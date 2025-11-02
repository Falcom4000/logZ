#include "Backend.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace logZ;

// 工作线程函数
void worker_thread(int thread_id, int num_logs) {
    // Queue 会自动注册（通过 thread_local QueueRegistration）
    // 线程退出时自动标记为 dying
    
    // 写日志
    for (int i = 0; i < num_logs; ++i) {
        LOG_INFO("Thread {} writing log {}", thread_id, i);
        LOG_DEBUG("Thread {} debug message {}", thread_id, i);
        LOG_WARN("Thread {} warning: value={}", thread_id, i * 100);
        
        // 模拟一些工作
        if(i % 100 == 0){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    std::cout << "Thread " << thread_id << " completed " << num_logs << " logs." << std::endl;
    
    LOG_INFO("Thread {} completed {} logs", thread_id, num_logs);
}

int main() {
    // 使用全局单例 Backend
    // Backend 会在第一次调用 get_instance() 时创建
    // 生命周期会自动延续到程序结束，确保比所有工作线程都长
    // IMPORTANT: 必须使用Logger::get_backend()来确保使用同一个Backend实例
    
    std::cout << "Getting backend instance..." << std::endl;
    auto& backend = Logger::get_backend();  // Use Logger's get_backend()
    
    std::cout << "Starting backend..." << std::endl;
    backend.start();
    
    // 让backend启动完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Writing first log..." << std::endl;
    LOG_INFO("=== Multi-threaded logging test started ===");
    
    // 创建多个线程
    const int num_threads = 4;
    const int logs_per_thread = 1000000;
    
    std::vector<std::thread> threads;
    
    LOG_INFO("Creating {} threads, each will write {} logs", num_threads, logs_per_thread);
    
    // 启动工作线程（不需要手动注册）
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i, logs_per_thread);
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "All threads joined." << std::endl;
    
    LOG_INFO("=== All threads completed ===");
    LOG_INFO("Total logs written: approximately {}", (num_threads * logs_per_thread * 3) + 5 + 3);
    
    // 等待日志写入完成
    std::cout << "Waiting for logs to be written..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "Stopping backend..." << std::endl;
    backend.stop();
    
    std::cout << "Program finished successfully!" << std::endl;    
    return 0;
}
