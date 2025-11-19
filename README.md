# logZ - 高性能异步日志库

## 项目背景

logZ 是一个专为**低延迟、高吞吐量**场景设计的 C++ 异步日志库。在高频交易、游戏服务器、实时系统等对性能要求极高的应用中，传统的同步日志库会显著影响主线程性能。logZ 通过以下设计理念解决这些问题：

### 核心目标
- **超低延迟**：日志调用对热路径（hot path）的影响降至最低（通常 < 100 CPU cycles）
- **无锁设计**：生产者线程完全无锁，避免锁竞争
- **零拷贝**：格式化和 I/O 操作异步执行，不阻塞业务线程
- **类型安全**：编译期类型检查和格式化字符串验证
- **可扩展性**：支持多线程并发写入，自动队列管理

---

## 设计框架

### 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                         Frontend (Logger)                        │
│  - 业务线程调用 LOG_INFO/DEBUG/ERROR 等宏                         │
│  - 编译期检查：最小日志级别过滤                                    │
│  - 编译期生成：格式化元信息和解码器                                │
└────────────────────────┬────────────────────────────────────────┘
                         │ 序列化（Encoder）
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Thread-Local Queue (SPSC)                     │
│  - 每个线程独立的无锁队列（Single Producer Single Consumer）       │
│  - 动态扩容：链式 RingBytes 节点（4KB → 64MB）                    │
│  - Backend 拥有所有 Queue，线程持有借用指针                       │
└────────────────────────┬────────────────────────────────────────┘
                         │ 双缓冲同步
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Backend (Consumer Thread)                     │
│  - 全局单例，后台线程轮询所有队列                                  │
│  - 时间戳排序：从多个队列中选择最小时间戳的日志                     │
│  - 反序列化（Decoder）：将二进制数据解码为可读字符串               │
│  - 延迟回收：线程退出后队列先排空再销毁                            │
└────────────────────────┬────────────────────────────────────────┘
                         │ 格式化输出
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│                    StringRingBuffer + Sinker                     │
│  - StringRingBuffer：格式化字符串的环形缓冲区                     │
│  - Sinker：文件 I/O（支持日志轮转、按日期分割）                    │
│  - 使用 POSIX write() + page cache，定期 fdatasync()             │
└─────────────────────────────────────────────────────────────────┘
```

### 关键设计决策

1. **前后端分离**：
   - **Frontend**（Logger）：快速序列化到内存队列
   - **Backend**（消费线程）：异步格式化和磁盘 I/O

2. **编译期优化**：
   - 格式化字符串作为模板参数（`FixedString`）
   - 每个日志点生成独立的解码器函数
   - 最小日志级别编译期过滤（`LOGZ_MIN_LEVEL`）

3. **内存管理**：
   - Backend 拥有所有 Queue 的 `unique_ptr`
   - 线程持有 `Queue*` 裸指针（借用）
   - 双缓冲 + 两阶段删除避免 use-after-free

4. **并发控制**：
   - 生产者：完全无锁（thread_local queue）
   - 消费者：单线程轮询，双缓冲同步队列列表

---

## 核心数据结构

### 1. **RingBytes** - 无锁环形缓冲区
```cpp
class RingBytes {
    size_t capacity_;              // 容量（2的幂）
    size_t capacity_mask_;         // 位掩码（用于快速取模）
    std::atomic<uint64_t> write_pos_;  // 写位置
    std::atomic<uint64_t> read_pos_;   // 读位置
    std::unique_ptr<std::byte[]> buffer_;
};
```

**特性**：
- SPSC（单生产者单消费者）模型
- 容量必须为 2 的幂（使用位运算优化取模）
- **不支持跨边界写入**：写入超过边界时返回 `nullptr`
- 预触发 page fault（4KB 页面预写入）避免运行时延迟

### 2. **Queue** - 动态扩容的队列
```cpp
class Queue {
    struct Node {
        std::unique_ptr<RingBytes> ring;
        std::atomic<Node*> next;
        size_t capacity;
    };
    std::atomic<Node*> write_node_;  // 当前写节点
    std::atomic<Node*> read_node_;   // 当前读节点
};
```

**特性**：
- 链表结构：多个 `RingBytes` 节点
- 自动扩容：当前节点满时分配 2倍容量的新节点（上限 64MB）
- 写满时**拒绝日志**（避免无限内存增长）
- 读空后自动回收旧节点

### 3. **Backend::QueueWrapper** - 队列生命周期管理
```cpp
struct QueueWrapper {
    std::unique_ptr<Queue> queue;       // 拥有 Queue
    std::atomic<bool> abandoned;        // 线程退出标记
    std::thread::id owner_thread_id;    // 所属线程 ID
    uint64_t created_timestamp;         // 创建时间
    uint64_t abandoned_timestamp;       // 废弃时间
};
```

**生命周期管理**：
```
1. 线程首次调用 LOG_XXX
   ↓
2. Backend::allocate_queue_for_thread()
   - 创建 Queue（Backend 拥有 unique_ptr）
   - 返回 Queue* 给线程（借用）
   - 加入 m_master_list（Copy-on-Write）
   ↓
3. 线程退出时 thread_local 析构
   ↓
4. Backend::mark_queue_orphaned()
   - 设置 orphaned = true
   - 不删除 Queue！
   ↓
5. Backend 定期检查
   - 发现 orphaned && queue->is_empty()
   ↓
6. 单阶段删除
   - 同步 m_snapshot_list
   - 从 m_current_list 移除
   - 再次同步 m_snapshot_list（确保不再引用）
   - 立即销毁队列
```

### 4. **Metadata** - 日志元数据
```cpp
struct Metadata {
    LogLevel level;           // 日志级别（1 byte）
    uint64_t timestamp;       // 纳秒时间戳
    uint32_t args_size;       // 参数序列化后的字节数
    DecoderFunc decoder;      // 解码器函数指针（编译期生成）
};
```

### 5. **StringRingBuffer** - 格式化输出缓冲
- 单线程环形缓冲区（Backend 专用）
- 支持动态扩容（2倍增长）
- 提供 `StringWriter` 接口用于 in-place 字符串构建

---

## 工作流程

### 1. **初始化阶段**
```cpp
auto& backend = Logger::get_backend();  // 获取全局单例
backend.start();                         // 启动后台消费线程
```

### 2. **日志写入（热路径）**
```cpp
LOG_INFO("User {} logged in with score {}", username, score);
```

**执行流程**：
```
1. 编译期检查：if constexpr (LogLevel::INFO >= MinLevel)
   ↓
2. 获取 thread_local Queue*（首次调用时从 Backend 分配）
   ↓
3. 计算序列化大小：sizeof(Metadata) + args_size
   ↓
4. Queue::reserve_write(total_size)
   - 成功：返回 buffer 指针
   - 失败（队列满）：丢弃日志，增加 dropped_count
   ↓
5. 编码到 buffer
   - 写入 Metadata（level, timestamp, args_size, decoder）
   - 序列化参数（POD 直接拷贝，字符串存储长度+内容/指针）
   ↓
6. 返回（耗时 < 100 cycles）
```

### 3. **后台消费（Backend 线程）**
```cpp
while (running_) {
    // 检查队列列表是否更新
    if (m_dirty.load()) sync_active_list();
    
    // 从所有队列中选择最小时间戳的日志
    process_one_log();
    
    // 定期刷盘和清理
    if (++counter >= 50000) {
        flush_to_disk();
        remove_orphaned_queues();
    }
}
```

**process_one_log() 详细流程**：
```
1. 遍历 m_active_list 中所有 QueueWrapper
   ↓
2. 对每个 Queue 调用 read(sizeof(Metadata)) peek 元数据
   ↓
3. 比较 timestamp，找到最小值
   ↓
4. 从选中的 Queue 读取完整日志
   - 读取 Metadata（commit）
   - 读取 args buffer（commit）
   ↓
5. 调用 decoder(args_buffer, writer)
   - 解码器是编译期生成的模板函数
   - 将二进制数据转换为格式化字符串
   ↓
6. 写入 StringRingBuffer
   ↓
7. 定期 flush 到 Sinker（文件）
```

### 4. **关闭流程**
```cpp
backend.stop();  // 等待所有日志处理完成
```
- 停止消费循环
- 处理剩余日志
- 回收所有队列
- 刷新所有缓冲区到磁盘

---

## Benchmark 性能测试

### 测试环境
- 测试代码：`benchmark/logZ.benchmark.cpp`
- 配置：4 个工作线程，每线程 100 万条日志
- 测量指标：日志调用延迟（CPU cycles，使用 RDTSC）

### 测试方法
```cpp
auto start = rdtsc();
LOG_INFO("Thread {} writing log {} with pi = {} and string {}", 
         thread_id, i, 3.1415 + i, s);
auto end = rdtsc();
latency[i] = (end - start);
```

### 性能指标
运行 `./run_perf_analysis.sh` 后可获得：
- **延迟分布**（P50/P95/P99/P99.9）
- **最小/最大/平均延迟**
- **延迟曲线图**（`data/latency_result.txt` + `plot_latency.py`）

### 典型结果（参考）
```
Min:     76 cycles     (序列化到队列的最小开销)
Medium:  114 cycles    (正常情况)
P99:     157 cycles    (队列扩容 + cache miss)
```

### 性能优化点
1. **预分配内存**：RingBytes 构造时预触发 page fault
2. **编译期优化**：日志级别过滤、格式化字符串编译期解析
3. **无锁队列**：thread_local Queue 避免锁竞争
4. **批量处理**：Backend 每 50000 次循环才 flush 一次

---

## 应用场景

### 1. **高频交易系统 (HFT)**
**需求**：
- 微秒级延迟敏感
- 需要详细审计日志（订单、成交、风控）
- 不能因为日志影响交易性能

**logZ 优势**：
- 日志调用 < 100 cycles（约 30-50 ns @ 3 GHz）
- 完全异步，不阻塞交易逻辑
- 时间戳精确到纳秒

**示例**：
```cpp
LOG_INFO("Order submitted: symbol={} price={} qty={} orderID={}", 
         symbol, price, quantity, order_id);
```


### 2. **实时音视频处理**
**需求**：
- 音频线程延迟极低（< 10ms）
- 需要记录帧处理、编解码耗时
- 调试性能瓶颈

**logZ 优势**：
- 日志调用不分配内存（队列预分配）
- 支持浮点数、自定义类型
- 可在音频回调中安全使用

**示例**：
```cpp
LOG_TRACE("Audio frame processed: samples={} latency={}us", 
          sample_count, latency_us);
```

**配置**：
```cpp
#define LOGZ_MIN_LEVEL ::logZ::LogLevel::WARN  // 只记录 WARN 及以上
```

---

## 编译和使用

### 使用 Bazel 构建
```bash
# 运行测试
bazel test //test:test_logger

# 运行 benchmark
bazel run //benchmark:logZ_benchmark

# 性能分析
./run_perf_analysis.sh
```

### 基本用法
```cpp
#include "Logger.h"

int main() {
    // 1. 启动 Backend
    auto& backend = logZ::Logger::get_backend();
    backend.start();
    
    // 2. 记录日志
    LOG_INFO("Application started");
    LOG_DEBUG("Debug value: {}", 42);
    LOG_ERROR("Error occurred: {}", error_message);
    
    // 3. 关闭（自动 flush）
    backend.stop();
    return 0;
}
```

### 配置选项
```cpp
// 编译期设置最小日志级别
#define LOGZ_MIN_LEVEL ::logZ::LogLevel::INFO

// Backend 配置
Backend<LogLevel::INFO> backend(
    "./logs",           // 日志目录
    1024 * 1024         // StringRingBuffer 大小
);

// 绑定 CPU 核心（避免调度延迟）
backend.start(2);  // 绑定到 CPU 核心 2
```

---

## 技术亮点

### 1. **编译期魔法**
- **FixedString**：格式化字符串作为模板参数
- **Decoder 生成**：每个日志点自动生成解码器函数
- **零运行时开销**：类型检查和格式解析在编译期完成

### 2. **内存安全**
- **所有权明确**：Backend 拥有 Queue，线程借用
- **两阶段删除**：避免 use-after-free
- **RAII 保证**：thread_local 析构自动标记队列废弃

### 3. **性能优化**
- **Cache Line 对齐**：防止 false sharing（`alignas(64)`）
- **分支预测提示**：`[[likely]]` / `[[unlikely]]`
- **位运算优化**：2 的幂容量 + 位掩码取代取模运算
- **预分配策略**：避免运行时 page fault

### 4. **可观测性**
- **丢失日志计数**：`backend.get_dropped_count()`
- **队列状态监控**：abandoned_timestamp、created_timestamp
- **延迟分析工具**：RDTSC + 百分位统计

---

## 文件结构

```
logZ/
├── include/               # 头文件
│   ├── Logger.h          # Frontend API（日志宏定义）
│   ├── Backend.h         # Backend 消费线程
│   ├── Queue.h           # 动态扩容队列
│   ├── RingBytes.h       # 无锁环形缓冲区
│   ├── Encoder.h         # 序列化（编译期优化）
│   ├── Decoder.h         # 反序列化（类型推导）
│   ├── StringRingBuffer.h # 格式化输出缓冲
│   ├── Sinker.h          # 文件 I/O
│   ├── LogTypes.h        # 公共类型定义
│   └── Fixedstring.h     # 编译期字符串
├── benchmark/
│   └── logZ.benchmark.cpp # 性能测试
├── test/                  # 单元测试
├── data/                  # 测试输出数据
├── plot_latency.py        # 延迟可视化脚本
└── run_perf_analysis.sh   # 一键性能分析
```
