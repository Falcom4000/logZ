# LogZ 并发日志基准测试

本基准测试使用 Google Benchmark 框架来测试 logZ 日志系统在8线程并发场景下的性能指标。

## 测试指标

1. **QPS (每秒查询数)**: 8个线程并发写日志的总吞吐量
2. **前端延迟**: 日志写入调用的延迟（从调用 LOG_INFO 到返回的时间）
   - 平均延迟
   - P50（中位数）
   - P95
   - P99
   - 最大延迟

## 基准测试说明

### 1. BM_ConcurrentLogging_8Threads
测试8个线程并发写入日志，每个线程每次迭代写入1000条日志。

**测量指标:**
- 吞吐量 (items/second)
- 平均前端延迟 (nanoseconds)

### 2. BM_ConcurrentLogging_VaryingLoad
测试不同负载下的延迟分布，测试场景包括：
- 每个线程100条日志
- 每个线程500条日志
- 每个线程1000条日志
- 每个线程5000条日志

**测量指标:**
- P50、P95、P99、最大延迟
- 吞吐量

### 3. BM_ConcurrentLogging_QPS
纯吞吐量测试，专注于测量最大 QPS。

## 编译和运行

### 编译基准测试

```bash
bazel build //benchmark:bench_concurrent_logging
```

### 运行基准测试

```bash
# 运行所有基准测试
bazel run //benchmark:bench_concurrent_logging

# 或者直接运行编译后的二进制文件
./bazel-bin/benchmark/bench_concurrent_logging
```

### 自定义运行参数

```bash
# 设置迭代次数
./bazel-bin/benchmark/bench_concurrent_logging --benchmark_repetitions=5

# 设置最小运行时间
./bazel-bin/benchmark/bench_concurrent_logging --benchmark_min_time=10s

# 以JSON格式输出结果
./bazel-bin/benchmark/bench_concurrent_logging --benchmark_format=json

# 输出到文件
./bazel-bin/benchmark/bench_concurrent_logging --benchmark_out=results.json --benchmark_out_format=json

# 只运行特定的基准测试
./bazel-bin/benchmark/bench_concurrent_logging --benchmark_filter=QPS
```

## 输出示例

```
Run on (8 X 2400 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 8192 KiB (x1)
------------------------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations items/s
------------------------------------------------------------------------------------
BM_ConcurrentLogging_8Threads      25.4 ms          8.2 ms          27  315k/s
  avg_frontend_latency_ns                                            250
BM_ConcurrentLogging_VaryingLoad/100  2.1 ms       0.7 ms         100  380k/s
  p50_latency_ns                                                     200
  p95_latency_ns                                                     450
  p99_latency_ns                                                     680
  max_latency_ns                                                     1200
...
```

## 性能分析

### 关键指标解读

1. **前端延迟 (Frontend Latency)**
   - 这是应用程序调用日志API的延迟
   - 理想情况下应该在几百纳秒以内
   - 如果延迟过高，可能是队列满了或者参数序列化开销大

2. **QPS (Queries Per Second)**
   - 衡量系统总体吞吐量
   - 8线程并发的总 QPS = 单线程 QPS × 线程数 × 并发效率
   - 高QPS意味着系统能处理更多的日志负载

3. **延迟分布**
   - P50: 50%的请求延迟在此值以下
   - P95: 95%的请求延迟在此值以下
   - P99: 99%的请求延迟在此值以下
   - 关注尾部延迟（P99、P999）对于系统稳定性很重要

## 优化建议

如果基准测试结果不理想，可以尝试：

1. **增大队列大小**: 修改 `Logger::get_thread_queue()` 中的队列大小
2. **增大输出缓冲区**: 修改 Backend 构造函数中的 buffer_size
3. **调整后端轮询间隔**: 修改 `consume_loop()` 中的 sleep 时间
4. **使用更快的磁盘**: SSD 比 HDD 快很多
5. **减少序列化开销**: 避免记录过大的数据结构

## 注意事项

- 基准测试会在当前目录生成 `benchmark.log` 文件
- 运行前请确保有足够的磁盘空间
- 多次运行取平均值以获得更准确的结果
- 不同硬件环境下的结果会有差异
