#include <gtest/gtest.h>
#include "Logger.h"
#include "Backend.h"
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <string>

using namespace logZ;

// Helper function to read log file content
std::string read_log_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper function to count occurrences of a substring
size_t count_occurrences(const std::string& str, const std::string& substr) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = str.find(substr, pos)) != std::string::npos) {
        ++count;
        pos += substr.length();
    }
    return count;
}

// Helper to remove log file if it exists
void remove_log_file(const std::string& filename) {
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }
}

// Test fixture for single-threaded tests
class SingleThreadLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file_ = "test_single_thread.log";
        remove_log_file(log_file_);
    }

    void TearDown() override {
        remove_log_file(log_file_);
    }

    std::string log_file_;
};

// Test fixture for multi-threaded tests
class MultiThreadLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file_ = "test_multi_thread.log";
        remove_log_file(log_file_);
    }

    void TearDown() override {
        remove_log_file(log_file_);
    }

    std::string log_file_;
};

// ============================================================
// Single-Thread Tests
// ============================================================

TEST_F(SingleThreadLoggerTest, SingleParameter_Integer) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Test integer: {}", 42);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Test integer: 42") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, SingleParameter_Double) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Test double: {}", 3.14159);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Test double: 3.14159") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, SingleParameter_StringLiteral) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Test string: {}", "hello");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Test string: hello") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, SingleParameter_StdString) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    std::string test_str = "std::string message";
    LOG_INFO("Test std::string: {}", test_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Test std::string: std::string message") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, SingleParameter_RuntimeCString) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    const char* c_str = "runtime c string";
    LOG_INFO("Test c_str: {}", c_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Test c_str: runtime c string") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, MultipleParameters_Mixed) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Mixed: int={} double={} string={}", 42, 3.14, "text");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Mixed: int=42 double=3.14 string=text") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, MultipleParameters_AllTypes) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    std::string std_str = "std_string";
    LOG_INFO("All types: {} {} {} {} {}", 100, 2.5, "literal", std_str, std_str.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("All types: 100 2.5 literal std_string std_string") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, NoParameters_FormatStringOnly) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Simple message without parameters");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Simple message without parameters") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, MultipleLogs_Sequential) {
    Backend<LogLevel::TRACE> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    for (int i = 0; i < 10; ++i) {
        LOG_INFO("Log entry {}", i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend.stop();

    std::string content = read_log_file(log_file_);
    for (int i = 0; i < 10; ++i) {
        std::string expected = "Log entry " + std::to_string(i);
        EXPECT_TRUE(content.find(expected) != std::string::npos) 
            << "Missing: " << expected;
    }
}

TEST_F(SingleThreadLoggerTest, DifferentLogLevels) {
    Backend<LogLevel::TRACE> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_TRACE("Trace message");
    LOG_DEBUG("Debug message");
    LOG_INFO("Info message");
    LOG_WARN("Warn message");
    LOG_ERROR("Error message");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Trace message") != std::string::npos);
    EXPECT_TRUE(content.find("Debug message") != std::string::npos);
    EXPECT_TRUE(content.find("Info message") != std::string::npos);
    EXPECT_TRUE(content.find("Warn message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, EmptyStringParameter) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    std::string empty_str = "";
    LOG_INFO("Empty string: '{}'", empty_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Empty string: ''") != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, LongString) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    std::string long_str(500, 'A');
    LOG_INFO("Long string: {}", long_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find(long_str) != std::string::npos);
}

TEST_F(SingleThreadLoggerTest, SpecialCharacters) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    backend.register_queue(&Logger::get_thread_queue());
    backend.start();

    LOG_INFO("Special chars: {} {} {}", "\n", "\t", "\\");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_file(log_file_);
    EXPECT_TRUE(content.find("Special chars:") != std::string::npos);
}

// ============================================================
// Multi-Thread Tests
// ============================================================

TEST_F(MultiThreadLoggerTest, TwoThreads_SimpleLog) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    
    std::vector<std::thread> threads;
    std::vector<Queue*> queues;
    std::mutex queue_mutex;
    
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&queue_mutex, &queues, t]() {
            // Get this thread's queue
            Queue* my_queue = &Logger::get_thread_queue();
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                queues.push_back(my_queue);
            }
            
            // Wait a bit to ensure registration happens first
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            for (int i = 0; i < 5; ++i) {
                LOG_INFO("Thread {} log {}", t, i);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Wait for threads to initialize their queues
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Register all queues from main thread
    for (Queue* q : queues) {
        backend.register_queue(q);
    }
    
    backend.start();
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend.stop();

    std::string content = read_log_file(log_file_);
    
    // Check that logs from both threads are present
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < 5; ++i) {
            std::string expected = "Thread " + std::to_string(t) + " log " + std::to_string(i);
            EXPECT_TRUE(content.find(expected) != std::string::npos)
                << "Missing: " << expected;
        }
    }
}

TEST_F(MultiThreadLoggerTest, FourThreads_MixedParameters) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    
    std::vector<std::thread> threads;
    std::vector<Queue*> queues;
    std::mutex queue_mutex;
    
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&queue_mutex, &queues, t]() {
            Queue* my_queue = &Logger::get_thread_queue();
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                queues.push_back(my_queue);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            for (int i = 0; i < 10; ++i) {
                LOG_INFO("T{} i={} d={} s={}", t, i, i * 0.5, "msg");
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (Queue* q : queues) {
        backend.register_queue(q);
    }
    
    backend.start();
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    backend.stop();

    std::string content = read_log_file(log_file_);
    
    // Verify total number of log entries (4 threads * 10 logs each)
    size_t log_count = count_occurrences(content, "[INFO]");
    EXPECT_GE(log_count, 40) << "Expected at least 40 log entries, got " << log_count;
}

TEST_F(MultiThreadLoggerTest, EightThreads_HighThroughput) {
    Backend<LogLevel::INFO> backend(log_file_, 4 * 1024 * 1024);  // 4MB buffer
    
    std::vector<std::thread> threads;
    std::vector<Queue*> queues;
    std::mutex queue_mutex;
    const int num_threads = 8;
    const int logs_per_thread = 100;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&queue_mutex, &queues, t, logs_per_thread]() {
            Queue* my_queue = &Logger::get_thread_queue();
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                queues.push_back(my_queue);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            for (int i = 0; i < logs_per_thread; ++i) {
                LOG_INFO("Thread {} iteration {}", t, i);
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    for (Queue* q : queues) {
        backend.register_queue(q);
    }
    
    backend.start();
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    backend.stop();

    std::string content = read_log_file(log_file_);
    
    // Verify we got a reasonable number of logs
    size_t log_count = count_occurrences(content, "[INFO]");
    EXPECT_GE(log_count, num_threads * logs_per_thread * 0.9)  // Allow 10% loss
        << "Expected at least " << (num_threads * logs_per_thread * 0.9) 
        << " log entries, got " << log_count;
}

TEST_F(MultiThreadLoggerTest, ConcurrentWithStdString) {
    Backend<LogLevel::INFO> backend(log_file_, 1024 * 1024);
    
    std::vector<std::thread> threads;
    std::vector<Queue*> queues;
    std::mutex queue_mutex;
    
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&queue_mutex, &queues, t]() {
            Queue* my_queue = &Logger::get_thread_queue();
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                queues.push_back(my_queue);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            for (int i = 0; i < 20; ++i) {
                std::string msg = "Message from thread " + std::to_string(t);
                LOG_INFO("Thread {}: {}", t, msg);
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (Queue* q : queues) {
        backend.register_queue(q);
    }
    
    backend.start();
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    backend.stop();

    std::string content = read_log_file(log_file_);
    
    // Verify logs from all threads are present
    for (int t = 0; t < 4; ++t) {
        std::string expected = "Message from thread " + std::to_string(t);
        EXPECT_TRUE(content.find(expected) != std::string::npos)
            << "Missing messages from thread " << t;
    }
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
