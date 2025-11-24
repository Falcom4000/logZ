#include "Backend.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <x86intrin.h>

using namespace logZ;

inline uint64_t rdtsc() {
    return __rdtsc();
}
// 工作线程函数
void worker_thread(int thread_id, int num_logs, std::vector<int>& latency, std::vector<double>& thread_durations) {
    std::string s = "test";
    auto thread_start = std::chrono::steady_clock::now();
    // 写日志
    for (int i = 0; i < num_logs; ++i) {
        s[3] = 'a' + (i % 26);
        auto start = rdtsc();
        LOG_INFO("Thread {} writing log {} with pi = {} and string {}", thread_id, i, 3.1415 +i, s);
        auto end = rdtsc();
        latency[i]= (end - start);
        // 模拟一些工作
        if(i % 1000 == 0){
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
    std::cout << "Thread " << thread_id << " completed " << num_logs << " logs." << std::endl;
    auto thread_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = thread_end - thread_start;
    thread_durations[thread_id] = elapsed.count();
}

int main() {
    // 创建多个线程
    constexpr int num_threads = 8;
    constexpr int logs_per_thread = 1000000;
    auto& backend = Logger::get_backend();  // Use Logger's get_backend()
    
    std::cout << "Starting backend..." << std::endl;
    backend.start();
    
    // 让backend启动完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Writing first log..." << std::endl;
    
    // 记录benchmark开始时间（用于测量吞吐量）
    auto benchmark_start = std::chrono::steady_clock::now();
    
    std::vector<std::thread> threads;
    std::vector<std::vector<int>> latencies(num_threads, std::vector<int>(logs_per_thread, 0));
    std::vector<double> thread_durations(num_threads, 0.0);
    // 启动工作线程（不需要手动注册）
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i, logs_per_thread, std::ref(latencies[i]), std::ref(thread_durations));
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "All threads joined." << std::endl;

    std::cout << "\n=== Thread Durations & QPS ===" << std::endl;
    double total_qps = 0.0;
    int total_logs = num_threads * logs_per_thread;
    for (int i = 0; i < num_threads; ++i) {
        double duration_sec = thread_durations[i];
        double qps = duration_sec > 0 ? logs_per_thread / duration_sec : 0.0;
        total_qps += qps;
        std::cout << "Thread " << i << ": " << duration_sec << " s, QPS = " << qps << std::endl;
    }
    std::cout << "Aggregate QPS (sum of per-thread): " << total_qps << std::endl;
    double benchmark_duration = *std::max_element(thread_durations.begin(), thread_durations.end());
    if (benchmark_duration > 0) {
        std::cout << "Overall QPS (total_logs / max_thread_time): "
                  << total_logs / benchmark_duration << std::endl;
    }
    
    // 等待后端处理完所有日志（等待log_count达到800万）
    std::cout << "\nWaiting for backend to process all logs..." << std::endl;
    const uint64_t expected_log_count = total_logs;  // 8 threads × 1,000,000 = 8,000,000
    
    while (backend.get_log_count() < expected_log_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 记录benchmark结束时间
    auto benchmark_end = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration<double>(benchmark_end - benchmark_start).count();
    
    // 获取最终日志计数
    uint64_t log_count = backend.get_log_count();
    
    // 计算后端吞吐量（Throughput）
    double throughput = (log_count > 0 && total_duration > 0) ? log_count / total_duration : 0.0;
    
    std::cout << "\n=== Backend Throughput ===" << std::endl;
    std::cout << "Total logs written by backend: " << log_count << std::endl;
    std::cout << "Expected logs: " << total_logs << std::endl;
    std::cout << "Total duration: " << std::fixed << std::setprecision(3) << total_duration << " s" << std::endl;
    std::cout << "Backend Throughput: " << std::fixed << std::setprecision(2) << throughput << " logs/sec" << std::endl;
    if (log_count > 0 && total_logs > 0) {
        std::cout << "Match rate: " << std::fixed << std::setprecision(2) 
                  << (100.0 * log_count / total_logs) << "%" << std::endl;
    }
    
    // 合并所有线程的latency数据到result
    std::vector<int> result;
    result.reserve(logs_per_thread * num_threads);
    for (int i = 0; i < num_threads; ++i) {
        result.insert(result.end(), latencies[i].begin(), latencies[i].end());
    }
    
    // 排序latency数据
    std::sort(result.begin(), result.end());
    
    // 创建data目录（如果不存在）
    struct stat st;
    if (stat("./data", &st) == -1) {
        mkdir("./data", 0755);
    }
    
    // 使用固定的文件名
    std::string filename = "./data/latency_result.txt";
    
    // 保存排序后的结果到文件
    std::ofstream outfile(filename);
    if (outfile.is_open()) {
        outfile << "# Latency data (CPU cycles)\n";
        outfile << "# Threads: " << num_threads << ", Logs per thread: " << logs_per_thread << "\n";
        outfile << "# Total samples: " << result.size() << "\n";
        outfile << "# Format: each line contains one latency value\n\n";
        
        for (const auto& latency : result) {
            outfile << latency << "\n";
        }
        
        outfile.close();
        std::cout << "Latency data saved to " << filename << std::endl;
        
        // 计算并打印统计信息
        if (!result.empty()) {
            size_t n = result.size();
            double sum = 0;
            for (const auto& val : result) {
                sum += val;
            }
            double avg = sum / n;
            
            std::cout << "\n=== Latency Statistics ===" << std::endl;
            std::cout << "Min: " << result.front() << " cycles" << std::endl;
            std::cout << "Max: " << result.back() << " cycles" << std::endl;
            std::cout << "Average: " << std::fixed << std::setprecision(2) << avg << " cycles" << std::endl;
            std::cout << "Median (p50): " << result[n/2] << " cycles" << std::endl;
            std::cout << "p95: " << result[size_t(n * 0.95)] << " cycles" << std::endl;
            std::cout << "p99: " << result[size_t(n * 0.99)] << " cycles" << std::endl;
            std::cout << "p99.9: " << result[size_t(n * 0.999)] << " cycles" << std::endl;
        }
    } else {
        std::cerr << "Failed to open output file: " << filename << std::endl;
    }
    
    std::cout << "Stopping backend..." << std::endl;
    backend.stop();
    
    std::cout << "Program finished successfully!" << std::endl;    
    return 0;
}
