#include <benchmark/benchmark.h>
#include "Logger.h"
#include "Backend.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace logZ;

// 全局后端实例
static Backend<LogLevel::INFO>* g_backend = nullptr;

// 初始化后端
static void InitBackend() {
    if (g_backend == nullptr) {
        g_backend = new Backend<LogLevel::INFO>("benchmark.log", 4 * 1024 * 1024);  // 4MB buffer
        g_backend->start();
    }
}

// 清理后端
static void CleanupBackend() {
    if (g_backend != nullptr) {
        g_backend->stop();
        delete g_backend;
        g_backend = nullptr;
    }
}

// 8线程并发写日志基准测试
static void BM_ConcurrentLogging_8Threads(benchmark::State& state) {
    // 每个线程在每次迭代中写入的日志数量
    const int logs_per_iteration = state.range(0);
    
    // 初始化后端（只在第一次迭代时执行）
    if (state.thread_index() == 0) {
        InitBackend();
        
        // 注册所有线程的队列
        std::vector<Queue*> queues;
        for (int i = 0; i < 8; ++i) {
            // 每个线程会有自己的队列，这里需要预先创建并注册
            // 注意：由于Queue是thread_local，我们需要让每个线程自己注册
        }
    }
    
    // 确保所有线程都已初始化
    if (state.thread_index() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 注册当前线程的队列到后端
    static thread_local bool registered = false;
    if (!registered) {
        g_backend->register_queue(&Logger::get_thread_queue());
        registered = true;
    }
    
    // 统计变量
    int64_t total_logs = 0;
    
    // 基准测试主循环
    for (auto _ : state) {
        // 每次迭代写入指定数量的日志
        for (int i = 0; i < logs_per_iteration; ++i) {
            // 记录前端延迟的开始时间
            auto start = std::chrono::high_resolution_clock::now();
            
            // 写日志（混合不同类型的参数以模拟真实场景）
            LOG_INFO("Benchmark log message", " thread_id=", state.thread_index(), 
                     " iteration=", i, " value=", 42.5, " count=", total_logs);
            
            // 记录前端延迟的结束时间
            auto end = std::chrono::high_resolution_clock::now();
            
            // 累加前端延迟
            auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            state.counters["frontend_latency_ns"] += latency_ns;
            
            total_logs++;
        }
    }
    
    // 设置每次迭代处理的日志数
    state.SetItemsProcessed(state.iterations() * logs_per_iteration);
    
    // 计算平均前端延迟
    if (state.iterations() > 0) {
        state.counters["avg_frontend_latency_ns"] = 
            state.counters["frontend_latency_ns"] / (state.iterations() * logs_per_iteration);
    }
    
    // 在所有线程完成后清理
    if (state.thread_index() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CleanupBackend();
    }
}

// 注册基准测试：8个线程，每次迭代每个线程写1000条日志
BENCHMARK(BM_ConcurrentLogging_8Threads)
    ->Threads(8)                    // 8个线程
    ->Arg(1000)                     // 每次迭代每个线程写1000条日志
    ->Unit(benchmark::kMillisecond) // 时间单位为毫秒
    ->UseRealTime();                // 使用实际时间（wall time）


// 不同日志数量的基准测试
static void BM_ConcurrentLogging_VaryingLoad(benchmark::State& state) {
    const int logs_per_iteration = state.range(0);
    
    if (state.thread_index() == 0) {
        InitBackend();
    }
    
    static thread_local bool registered = false;
    if (!registered) {
        g_backend->register_queue(&Logger::get_thread_queue());
        registered = true;
    }
    
    std::vector<int64_t> latencies;
    latencies.reserve(logs_per_iteration);
    
    for (auto _ : state) {
        for (int i = 0; i < logs_per_iteration; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            LOG_INFO("Test message ", i, " with data: ", 3.14159, " status=", true);
            
            auto end = std::chrono::high_resolution_clock::now();
            latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        }
        
        // 计算延迟统计
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            state.counters["p50_latency_ns"] = latencies[latencies.size() / 2];
            state.counters["p95_latency_ns"] = latencies[latencies.size() * 95 / 100];
            state.counters["p99_latency_ns"] = latencies[latencies.size() * 99 / 100];
            state.counters["max_latency_ns"] = latencies.back();
        }
        
        latencies.clear();
    }
    
    state.SetItemsProcessed(state.iterations() * logs_per_iteration);
    
    if (state.thread_index() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CleanupBackend();
    }
}

// 测试不同负载：100, 500, 1000, 5000条日志
BENCHMARK(BM_ConcurrentLogging_VaryingLoad)
    ->Threads(8)
    ->Args({100})
    ->Args({500})
    ->Args({1000})
    ->Args({5000})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();


// 简单的QPS测试（只关注吞吐量）
static void BM_ConcurrentLogging_QPS(benchmark::State& state) {
    if (state.thread_index() == 0) {
        InitBackend();
    }
    
    static thread_local bool registered = false;
    if (!registered) {
        g_backend->register_queue(&Logger::get_thread_queue());
        registered = true;
    }
    
    int64_t count = 0;
    
    for (auto _ : state) {
        LOG_INFO("QPS test message ", count++, " data=", 123.456);
    }
    
    state.SetItemsProcessed(state.iterations());
    
    if (state.thread_index() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CleanupBackend();
    }
}

// QPS测试：8线程并发
BENCHMARK(BM_ConcurrentLogging_QPS)
    ->Threads(8)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();


BENCHMARK_MAIN();
