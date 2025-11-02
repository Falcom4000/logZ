#include <benchmark/benchmark.h>
#include "../include/Logger.h"
#include "../include/Backend.h"
#include <thread>
#include <vector>
#include <string>
#include <x86intrin.h>  // For __rdtsc()

using namespace logZ;

// 读取CPU时间戳计数器（TSC）
inline uint64_t rdtsc() {
    return __rdtsc();
}

// 全局Backend实例
static Backend<LogLevel::TRACE>* g_backend = nullptr;

// 初始化函数（benchmark开始前调用）
static void SetupBackend(const benchmark::State& state) {
    if (state.thread_index() == 0 && g_backend == nullptr) {
        g_backend = &Logger::get_backend();
        g_backend->start(0);  // 绑定到CPU核心0
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 清理函数（benchmark结束后调用）
static void TeardownBackend(const benchmark::State& state) {
    if (state.thread_index() == 0 && g_backend != nullptr) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        g_backend->stop();
    }
}

// ============================================================================
// Benchmark 1: 写入整数和浮点数
// 4线程并发，每线程写1M条日志
// ============================================================================
static void BM_ConcurrentLogging_IntDouble(benchmark::State& state) {
    SetupBackend(state);
    
    // Reset dropped counter before benchmark
    if (state.thread_index() == 0) {
        g_backend->reset_dropped_count();
    }
    
    int i = 42;
    double j = 3.14159;
    uint64_t total_cycles = 0;
    uint64_t iterations = 0;
    
    for (auto _ : state) {
        uint64_t start = rdtsc();
        LOG_INFO("Thread {} writes int={} double={}", state.thread_index(), i, j);
        uint64_t end = rdtsc();
        
        total_cycles += (end - start);
        iterations++;
        i++;
        j += 0.1;
    }
    
    // 报告平均每次日志的CPU周期数
    state.counters["cycles_per_log"] = benchmark::Counter(
        static_cast<double>(total_cycles) / iterations,
        benchmark::Counter::kAvgThreads
    );
    state.counters["total_cycles"] = benchmark::Counter(
        total_cycles,
        benchmark::Counter::kIsRate
    );
    
    // 报告丢包率
    if (state.thread_index() == 0) {
        uint64_t dropped = g_backend->get_dropped_count();
        uint64_t total_attempts = iterations * state.threads();
        double loss_rate = (total_attempts > 0) ? 
            (static_cast<double>(dropped) / total_attempts * 100.0) : 0.0;
        
        state.counters["dropped_msgs"] = benchmark::Counter(
            static_cast<double>(dropped),
            benchmark::Counter::kAvgThreads
        );
        state.counters["loss_rate_%"] = benchmark::Counter(
            loss_rate,
            benchmark::Counter::kAvgThreads
        );
    }
    
    TeardownBackend(state);
}

// 注册benchmark：4线程，每线程1M次迭代
BENCHMARK(BM_ConcurrentLogging_IntDouble)
    ->Threads(4)
    ->Iterations(1000000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark 2: 写入字符串
// 4线程并发，每线程写1M条日志
// ============================================================================
static void BM_ConcurrentLogging_String(benchmark::State& state) {
    SetupBackend(state);
    
    // Reset dropped counter before benchmark
    if (state.thread_index() == 0) {
        g_backend->reset_dropped_count();
    }
    
    std::string s = "This is a test message with some content";
    uint64_t total_cycles = 0;
    uint64_t iterations = 0;
    
    for (auto _ : state) {
        uint64_t start = rdtsc();
        LOG_INFO("Thread {} writes string: {}", state.thread_index(), s);
        uint64_t end = rdtsc();
        
        total_cycles += (end - start);
        iterations++;
    }
    
    // 报告平均每次日志的CPU周期数
    state.counters["cycles_per_log"] = benchmark::Counter(
        static_cast<double>(total_cycles) / iterations,
        benchmark::Counter::kAvgThreads
    );
    state.counters["total_cycles"] = benchmark::Counter(
        total_cycles,
        benchmark::Counter::kIsRate
    );
    
    // 报告丢包率
    if (state.thread_index() == 0) {
        uint64_t dropped = g_backend->get_dropped_count();
        uint64_t total_attempts = iterations * state.threads();
        double loss_rate = (total_attempts > 0) ? 
            (static_cast<double>(dropped) / total_attempts * 100.0) : 0.0;
        
        state.counters["dropped_msgs"] = benchmark::Counter(
            static_cast<double>(dropped),
            benchmark::Counter::kAvgThreads
        );
        state.counters["loss_rate_%"] = benchmark::Counter(
            loss_rate,
            benchmark::Counter::kAvgThreads
        );
    }
    
    TeardownBackend(state);
}

// 注册benchmark：4线程，每线程1M次迭代
BENCHMARK(BM_ConcurrentLogging_String)
    ->Threads(4)
    ->Iterations(1000000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark 3: 混合类型（对比）
// 4线程并发，每线程1M条日志
// ============================================================================
static void BM_ConcurrentLogging_Mixed(benchmark::State& state) {
    SetupBackend(state);
    
    int count = 0;
    double value = 1.5;
    std::string name = "worker";
    
    for (auto _ : state) {
        LOG_INFO("Thread {} {} count={} value={}", state.thread_index(), name, count, value);
        count++;
        value += 0.5;
    }
    
    TeardownBackend(state);
}

// 注册benchmark：4线程，每线程1M次迭代
BENCHMARK(BM_ConcurrentLogging_Mixed)
    ->Threads(4)
    ->Iterations(1000000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark 4: 小数据量对比（单线程）
// ============================================================================
static void BM_SingleThread_IntDouble(benchmark::State& state) {
    SetupBackend(state);
    
    int i = 0;
    double j = 0.0;
    
    for (auto _ : state) {
        LOG_INFO("Single thread int={} double={}", i++, j);
        j += 0.1;
    }
    
    TeardownBackend(state);
}

BENCHMARK(BM_SingleThread_IntDouble)
    ->Iterations(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_SingleThread_String(benchmark::State& state) {
    SetupBackend(state);
    
    std::string s = "Test message";
    
    for (auto _ : state) {
        LOG_INFO("Single thread string: {}", s);
    }
    
    TeardownBackend(state);
}

BENCHMARK(BM_SingleThread_String)
    ->Iterations(100000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
