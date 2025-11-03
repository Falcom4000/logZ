#include <gtest/gtest.h>
#include "Logger.h"
#include "Backend.h"
#include <thread>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>

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

// Helper to remove log file or directory if it exists
void remove_log_path(const std::string& path) {
    if (std::filesystem::exists(path)) {
        if (std::filesystem::is_directory(path)) {
            std::filesystem::remove_all(path);
        } else {
            std::filesystem::remove(path);
        }
    }
}

// Helper to get log content from directory (reads first log file found)
std::string read_log_from_dir(const std::string& log_dir) {
    if (!std::filesystem::exists(log_dir)) {
        return "";
    }
    
    // Find first .log file in directory
    for (const auto& entry : std::filesystem::directory_iterator(log_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            return read_log_file(entry.path().string());
        }
    }
    
    return "";
}

// Test fixture
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_dir_ = "./test_logs";
        remove_log_path(log_dir_);
    }

    void TearDown() override {
        auto& backend = Logger::get_backend();
        backend.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        remove_log_path(log_dir_);
    }

    std::string log_dir_;
};

// ============================================================
// Basic Tests
// ============================================================

TEST_F(LoggerTest, BasicInteger) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Test integer: {}", 42);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Test integer: 42") != std::string::npos);
}

TEST_F(LoggerTest, BasicDouble) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Test double: {}", 3.14159);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Test double: 3.14159") != std::string::npos);
}

TEST_F(LoggerTest, BasicString) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Test string: {}", "hello");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Test string: hello") != std::string::npos);
}

TEST_F(LoggerTest, MixedParameters) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Mixed: int={} double={} string={}", 42, 3.14, "text");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Mixed: int=42 double=3.14 string=text") != std::string::npos);
}

// ============================================================
// String Type Tests - All variations
// ============================================================

TEST_F(LoggerTest, String_CompileTimeLiteral) {
    auto& backend = Logger::get_backend();
    backend.start();

    // Compile-time string literal - should use FixedString optimization
    LOG_INFO("Literal: {}", "compile_time_literal");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Literal: compile_time_literal") != std::string::npos);
}

TEST_F(LoggerTest, String_CompileTimeLiteral_Empty) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Empty literal: {}", "");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Empty literal: ") != std::string::npos);
}

TEST_F(LoggerTest, String_CompileTimeLiteral_Long) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_INFO("Long literal: {}", "This is a very long compile-time string literal for testing purposes");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Long literal: This is a very long compile-time string literal for testing purposes") != std::string::npos);
}

TEST_F(LoggerTest, String_StdString) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string test_str = "std::string message";
    LOG_INFO("std::string: {}", test_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("std::string: std::string message") != std::string::npos);
}

TEST_F(LoggerTest, String_StdString_Empty) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string empty_str = "";
    LOG_INFO("Empty std::string: {}", empty_str);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Empty std::string: ") != std::string::npos);
}

TEST_F(LoggerTest, String_StdString_Modified) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string str = "initial";
    LOG_INFO("Before: {}", str);
    str = "modified";  // Modify after logging - should not affect logged value
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Before: initial") != std::string::npos);
    EXPECT_FALSE(content.find("Before: modified") != std::string::npos);
}

TEST_F(LoggerTest, String_StringView) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string_view sv = "string_view content";
    LOG_INFO("string_view: {}", sv);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("string_view: string_view content") != std::string::npos);
}

TEST_F(LoggerTest, String_StringView_FromStdString) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string str = "base string";
    std::string_view sv(str);
    LOG_INFO("string_view from std::string: {}", sv);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("string_view from std::string: base string") != std::string::npos);
}

TEST_F(LoggerTest, String_StringView_Substring) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string str = "Hello World";
    std::string_view sv(str.data() + 6, 5);  // "World"
    LOG_INFO("Substring view: {}", sv);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Substring view: World") != std::string::npos);
}

TEST_F(LoggerTest, String_CharArray) {
    auto& backend = Logger::get_backend();
    backend.start();

    char char_arr[] = "char array content";
    LOG_INFO("char[]: {}", char_arr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("char[]: char array content") != std::string::npos);
}

TEST_F(LoggerTest, String_CharArray_Modified) {
    auto& backend = Logger::get_backend();
    backend.start();

    char buffer[20] = "original";
    LOG_INFO("Buffer: {}", buffer);
    strcpy(buffer, "changed");  // Should not affect logged value
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Buffer: original") != std::string::npos);
}

TEST_F(LoggerTest, String_ConstCharPtr_Literal) {
    auto& backend = Logger::get_backend();
    backend.start();

    const char* cstr = "const char* literal";
    LOG_INFO("const char*: {}", cstr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("const char*: const char* literal") != std::string::npos);
}

TEST_F(LoggerTest, String_ConstCharPtr_FromStdString) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string str = "from std::string";
    const char* cstr = str.c_str();
    LOG_INFO("const char* from c_str(): {}", cstr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("const char* from c_str(): from std::string") != std::string::npos);
}

TEST_F(LoggerTest, String_CharPtr_RuntimeBuffer) {
    auto& backend = Logger::get_backend();
    backend.start();

    char buffer[50];
    snprintf(buffer, sizeof(buffer), "runtime %d", 123);
    LOG_INFO("Runtime buffer: {}", buffer);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("Runtime buffer: runtime 123") != std::string::npos);
}

TEST_F(LoggerTest, String_AllTypesMixed) {
    auto& backend = Logger::get_backend();
    backend.start();

    std::string std_str = "std_string";
    std::string_view sv = "string_view";
    char char_arr[] = "char_array";
    const char* cstr = "const_char_ptr";

    LOG_INFO("All string types: literal={} std::string={} string_view={} char[]={} const char*={}", 
             "literal", std_str, sv, char_arr, cstr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    EXPECT_TRUE(content.find("All string types: literal=literal std::string=std_string string_view=string_view char[]=char_array const char*=const_char_ptr") != std::string::npos);
}

TEST_F(LoggerTest, MultiThreadBasic) {
    auto& backend = Logger::get_backend();
    backend.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr int num_threads = 4;
    constexpr int logs_per_thread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                LOG_INFO("Thread {} log {}", i, j);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    
    // Check that we have logs from all threads
    for (int i = 0; i < num_threads; ++i) {
        std::string search_str = "Thread " + std::to_string(i) + " log";
        EXPECT_TRUE(content.find(search_str) != std::string::npos);
    }
}

TEST_F(LoggerTest, DifferentLogLevels) {
    auto& backend = Logger::get_backend();
    backend.start();

    LOG_TRACE("Trace message");
    LOG_DEBUG("Debug message");
    LOG_INFO("Info message");
    LOG_WARN("Warning message");
    LOG_ERROR("Error message");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backend.stop();

    std::string content = read_log_from_dir("./logs");
    
    // Default MinLevel is INFO, so TRACE and DEBUG should not appear
    EXPECT_TRUE(content.find("Info message") != std::string::npos);
    EXPECT_TRUE(content.find("Warning message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
