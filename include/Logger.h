#pragma once

#include "Queue.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <memory>

namespace logZ {

// Compile-time string concatenation macros for "[filename:line functionname]" format
#define LOGZ_LOCATION_STR_IMPL(file, line, func) "[" file ":" #line " " func "]"
#define LOGZ_LOCATION_STR(file, line, func) LOGZ_LOCATION_STR_IMPL(file, line, func)

// Helper macro to extract filename from __FILE__
#define LOGZ_FILENAME(file) \
    (::logZ::extract_filename(file))

// Compile-time filename extraction
constexpr const char* extract_filename(const char* path) noexcept {
    const char* filename = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            filename = p + 1;
        }
    }
    return filename;
}

// Compile-time string length calculation
constexpr size_t const_strlen(const char* str) noexcept {
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return len;
}

/**
 * @brief Log levels
 */
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

/**
 * @brief Log metadata stored at the beginning of each log entry
 */
struct LogMetadata {
    LogLevel level;
    uint64_t timestamp;      // Timestamp in nanoseconds
    uint32_t location_len;   // Length of location string [filename:line functionname]
    uint32_t args_size;      // Size of serialized arguments
};

/**
 * @brief Logger frontend - puts messages into queue
 * @tparam MinLevel Minimum log level (compile-time constant)
 */
template<LogLevel MinLevel = LogLevel::INFO>
class Logger {
public:
    /**
     * @brief Constructor
     * @param queue Reference to the queue for storing log messages
     */
    explicit Logger(Queue& queue)
        : queue_(queue) {
    }

    /**
     * @brief Get the minimum log level (compile-time constant)
     */
    static constexpr LogLevel get_level() {
        return MinLevel;
    }

    /**
     * @brief Check if a log level should be logged (compile-time)
     */
    static constexpr bool should_log(LogLevel level) {
        return level >= MinLevel;
    }

    /**
     * @brief Log a message with variadic template parameters
     * @tparam Level Log level (compile-time constant)
     * @tparam Args Types of arguments to log
     * @param location Source location string (pre-formatted at compile time)
     * @param args Arguments to serialize and log
     */
    template<LogLevel Level, typename... Args>
    void log_impl(const char* location, const Args&... args) {
        if constexpr (!should_log(Level)) {
            return;
        }

        // Get location string length (will be optimized by compiler for string literals)
        size_t location_len = const_strlen(location);

        // Calculate user data size
        size_t user_data_size = calculate_args_size(args...);

        // Calculate total size needed
        size_t total_size = sizeof(LogMetadata) + location_len + user_data_size;

        // Reserve space in queue
        std::byte* buffer = queue_.reserve_write(total_size);
        if (buffer == nullptr) {
            // Queue is full, log message lost
            // TODO: Add metrics for dropped messages
            return;
        }

        // Write metadata
        LogMetadata* metadata = reinterpret_cast<LogMetadata*>(buffer);
        metadata->level = Level;
        metadata->timestamp = get_timestamp_ns();
        metadata->location_len = static_cast<uint32_t>(location_len);
        metadata->args_size = static_cast<uint32_t>(user_data_size);

        // Write location string "[filename:line functionname]"
        std::byte* ptr = buffer + sizeof(LogMetadata);
        std::memcpy(ptr, location, location_len);
        ptr += location_len;

        // Serialize arguments into buffer
        if (user_data_size > 0) {
            serialize_args(ptr, args...);
        }

        // Commit write
        queue_.commit_write(total_size);
    }

private:
    /**
     * @brief Calculate the size needed for log arguments
     */
    template<typename... Args>
    static size_t calculate_args_size(const Args&... args) {
        // TODO: Implement actual calculation
        // For now, return 0
        (void)sizeof...(args); // Suppress unused parameter warning
        return 0;
    }

    /**
     * @brief Serialize arguments into buffer
     */
    template<typename... Args>
    static void serialize_args(std::byte* buffer, const Args&... args) {
        // TODO: Implement actual serialization
        (void)buffer;
        (void)sizeof...(args); // Suppress unused parameter warning
    }

    /**
     * @brief Get current timestamp in nanoseconds
     */
    static uint64_t get_timestamp_ns() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }

    /**
     * @brief Get current thread ID
     */
    static uint32_t get_thread_id() {
        auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return static_cast<uint32_t>(tid);
    }

    Queue& queue_;
};

/**
 * @brief Thread-local logger storage
 */
template<LogLevel MinLevel = LogLevel::INFO>
class ThreadLocalLogger {
public:
    /**
     * @brief Get the thread-local logger instance
     */
    static Logger<MinLevel>& get_instance() {
        thread_local std::unique_ptr<Queue> queue = std::make_unique<Queue>(4096);
        thread_local Logger<MinLevel> logger(*queue);
        return logger;
    }

    // Delete copy and move
    ThreadLocalLogger() = delete;
    ThreadLocalLogger(const ThreadLocalLogger&) = delete;
    ThreadLocalLogger& operator=(const ThreadLocalLogger&) = delete;
    ThreadLocalLogger(ThreadLocalLogger&&) = delete;
    ThreadLocalLogger& operator=(ThreadLocalLogger&&) = delete;
};

} // namespace logZ

// Logging macros - use thread-local logger
#define LOG_TRACE(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<::logZ::LogLevel::TRACE>::get_instance().should_log(::logZ::LogLevel::TRACE)) { \
            ::logZ::ThreadLocalLogger<::logZ::LogLevel::TRACE>::get_instance().log_impl< \
                ::logZ::LogLevel::TRACE>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)

#define LOG_DEBUG(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<::logZ::LogLevel::DEBUG>::get_instance().should_log(::logZ::LogLevel::DEBUG)) { \
            ::logZ::ThreadLocalLogger<::logZ::LogLevel::DEBUG>::get_instance().log_impl< \
                ::logZ::LogLevel::DEBUG>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)

#define LOG_INFO(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<>::get_instance().should_log(::logZ::LogLevel::INFO)) { \
            ::logZ::ThreadLocalLogger<>::get_instance().log_impl< \
                ::logZ::LogLevel::INFO>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)

#define LOG_WARN(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<>::get_instance().should_log(::logZ::LogLevel::WARN)) { \
            ::logZ::ThreadLocalLogger<>::get_instance().log_impl< \
                ::logZ::LogLevel::WARN>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)

#define LOG_ERROR(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<>::get_instance().should_log(::logZ::LogLevel::ERROR)) { \
            ::logZ::ThreadLocalLogger<>::get_instance().log_impl< \
                ::logZ::LogLevel::ERROR>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)

#define LOG_FATAL(...) \
    do { \
        if constexpr (::logZ::ThreadLocalLogger<>::get_instance().should_log(::logZ::LogLevel::FATAL)) { \
            ::logZ::ThreadLocalLogger<>::get_instance().log_impl< \
                ::logZ::LogLevel::FATAL>( \
                LOGZ_LOCATION_STR(LOGZ_FILENAME(__FILE__), __LINE__, __FUNCTION__), \
                __VA_ARGS__); \
        } \
    } while(0)
