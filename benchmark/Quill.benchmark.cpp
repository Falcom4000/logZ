#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <x86intrin.h>

using namespace std::chrono_literals;

inline uint64_t rdtsc() {
    return __rdtsc();
}

void worker_thread(int thread_id,
                   int num_logs,
                   std::vector<int>& latency,
                   quill::Logger* logger,
                   std::vector<double>& thread_durations) {
    std::string s = "test";
    auto thread_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_logs; ++i) {
        s[3] = 'a' + (i % 26);
        auto start = rdtsc();
        LOG_INFO(logger, "Thread {} writing log {} with pi = {} and string {}", thread_id, i, 3.1415 + i, s);
        auto end = rdtsc();
        latency[i] = static_cast<int>(end - start);

        if (i % 1000 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    auto thread_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = thread_end - thread_start;
    thread_durations[thread_id] = elapsed.count();
    std::cout << "Thread " << thread_id << " completed " << num_logs << " logs." << std::endl;
}

int main() {
    constexpr int num_threads = 8;
    constexpr int logs_per_thread = 1'000'000;
    constexpr char kDataDir[] = "./data";
    constexpr char kOutputFile[] = "./data/quill_latency_result.txt";

    quill::Backend::start();

    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>("logs/quill_benchmark.log");
    auto logger = quill::Frontend::create_or_get_logger("quill_bench", std::move(file_sink));
    logger->set_log_level(quill::LogLevel::Info);

    std::cout << "Waiting backend to warm up..." << std::endl;
    std::this_thread::sleep_for(100ms);

    std::vector<std::thread> threads;
    std::vector<std::vector<int>> latencies(num_threads, std::vector<int>(logs_per_thread, 0));
    std::vector<double> thread_durations(num_threads, 0.0);

    auto bench_start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i, logs_per_thread, std::ref(latencies[i]), logger, std::ref(thread_durations));
    }

    for (auto& t : threads) {
        t.join();
    }
    auto bench_end = std::chrono::steady_clock::now();
    std::cout << "All threads joined." << std::endl;

    auto bench_duration = std::chrono::duration<double>(bench_end - bench_start).count();

    std::cout << "\n=== Thread Durations & QPS (Quill) ===" << std::endl;
    double aggregate_qps = 0.0;
    const int total_logs = num_threads * logs_per_thread;
    for (int i = 0; i < num_threads; ++i) {
        double duration_sec = thread_durations[i];
        double qps = duration_sec > 0 ? logs_per_thread / duration_sec : 0.0;
        aggregate_qps += qps;
        std::cout << "Thread " << i << ": " << duration_sec << " s, QPS = " << qps << std::endl;
    }
    std::cout << "Aggregate QPS (sum of per-thread): " << aggregate_qps << std::endl;
    if (bench_duration > 0) {
        std::cout << "Overall QPS (total_logs / bench_duration): " << total_logs / bench_duration << std::endl;
    }

    std::vector<int> result;
    result.reserve(total_logs);
    for (int i = 0; i < num_threads; ++i) {
        result.insert(result.end(), latencies[i].begin(), latencies[i].end());
    }
    std::sort(result.begin(), result.end());

    struct stat st;
    if (stat(kDataDir, &st) == -1) {
        mkdir(kDataDir, 0755);
    }

    std::ofstream outfile(kOutputFile);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open output file: " << kOutputFile << std::endl;
        quill::Backend::stop();
        return 1;
    }

    outfile << "# Quill Latency data (CPU cycles)\n";
    outfile << "# Threads: " << num_threads << ", Logs per thread: " << logs_per_thread << "\n";
    outfile << "# Total samples: " << result.size() << "\n\n";
    for (const auto& latency : result) {
        outfile << latency << "\n";
    }
    outfile.close();
    std::cout << "Latency data saved to " << kOutputFile << std::endl;

    if (!result.empty()) {
        size_t n = result.size();
        double sum = 0;
        for (const auto& val : result) {
            sum += val;
        }
        double avg = sum / n;
        auto percentile = [&](double p) -> int {
            size_t idx = static_cast<size_t>(p * n);
            if (idx >= n) idx = n - 1;
            return result[idx];
        };

        std::cout << "\n=== Latency Statistics (Quill) ===" << std::endl;
        std::cout << "Min: " << result.front() << " cycles" << std::endl;
        std::cout << "Max: " << result.back() << " cycles" << std::endl;
        std::cout << "Average: " << std::fixed << std::setprecision(2) << avg << " cycles" << std::endl;
        std::cout << "Median (p50): " << percentile(0.5) << " cycles" << std::endl;
        std::cout << "p95: " << percentile(0.95) << " cycles" << std::endl;
        std::cout << "p99: " << percentile(0.99) << " cycles" << std::endl;
        std::cout << "p99.9: " << percentile(0.999) << " cycles" << std::endl;
    }

    std::cout << "\nWaiting for Quill backend to drain..." << std::endl;
    std::this_thread::sleep_for(5s);
    quill::Backend::stop();
    std::cout << "Program finished successfully!" << std::endl;
    return 0;
}

