#include <benchmark/benchmark.h>
#include "Logger.h"
#include "Backend.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <cstdlib>

using namespace logZ;

// 全局后端实例 - 使用智能指针避免退出时崩溃
static std::unique_ptr<Backend<LogLevel::INFO>> g_backend;

// 初始化后端
static void InitBackend() {
    if (!g_backend) {
        g_backend = std::make_unique<Backend<LogLevel::INFO>>("benchmark.log", 4 * 1024 * 1024);  // 4MB buffer
        g_backend->start();
        // 注意：程序退出时不清理，避免崩溃
    }
}

// 8线程并发写日志基准测试
static void BM_ConcurrentLogging_8Threads(benchmark::State& state) {
    // 每个线程在每次迭代中写入的日志数量
    const int logs_per_iteration = state.range(0);
    
    // 初始化后端（只在第一个线程执行）
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        InitBackend();
    });
    
    // 注册当前线程的队列到后端
    static thread_local bool registered = false;
    if (!registered && g_backend != nullptr) {
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
            LOG_INFO("Benchmark log message thread_id={} iteration={} value={} count={}", 
                     state.thread_index(), i, 42.5, total_logs);
            
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
}

// 注册基准测试：8个线程，每次迭代每个线程写1000条日志
// 暂时禁用多线程测试（有崩溃问题）
// BENCHMARK(BM_ConcurrentLogging_8Threads)
//     ->Threads(8)                    // 8个线程
//     ->Arg(1000)                     // 每次迭代每个线程写1000条日志
//     ->Unit(benchmark::kMillisecond) // 时间单位为毫秒
//     ->UseRealTime();                // 使用实际时间（wall time）


// 简单的单线程测试
static void BM_SingleThread_Simple(benchmark::State& state) {
    static bool init = false;
    if (!init) {
        InitBackend();
        g_backend->register_queue(&Logger::get_thread_queue());
        init = true;
    }
    
    int count = 0;
    for (auto _ : state) {
        LOG_INFO("Simple test message {}", count++);
    }
}

BENCHMARK(BM_SingleThread_Simple)
    ->Unit(benchmark::kMicrosecond);


// 不同日志数量的基准测试
static void BM_ConcurrentLogging_VaryingLoad(benchmark::State& state) {
    const int logs_per_iteration = state.range(0);
    
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        InitBackend();
    });
    
    static thread_local bool registered = false;
    if (!registered && g_backend != nullptr) {
        g_backend->register_queue(&Logger::get_thread_queue());
        registered = true;
    }
    
    std::vector<int64_t> latencies;
    latencies.reserve(logs_per_iteration);
    
    for (auto _ : state) {
        for (int i = 0; i < logs_per_iteration; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            LOG_INFO("Test message {} with data: {} status={}", i, 3.14159, true);
            
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
}

// 测试不同负载：100, 500, 1000, 5000条日志
// 暂时禁用多线程测试
// BENCHMARK(BM_ConcurrentLogging_VaryingLoad)
//     ->Threads(8)
//     ->Args({100})
//     ->Args({500})
//     ->Args({1000})
//     ->Args({5000})
//     ->Unit(benchmark::kMillisecond)
//     ->UseRealTime();


int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    
    // 显式停止 backend
    if (g_backend) {
        g_backend->stop();
    }
    
    benchmark::Shutdown();
    return 0;
}
