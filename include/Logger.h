#pragma once

#include "LogTypes.h"
#include "Queue.h"
#include "Decoder.h"
#include "Encoder.h"
#include "Fixedstring.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <memory>

namespace logZ {

// Forward declarations
template<LogLevel MinLevel>
class Backend;

template<LogLevel MinLevel>
class QueueRegistration;

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

// Compile-time minimum log level configuration
// Change this to adjust minimum log level at compile time
#ifndef LOGZ_MIN_LEVEL
#define LOGZ_MIN_LEVEL ::logZ::LogLevel::TRACE
#endif

/**
 * @brief Logger frontend - puts messages into queue
 * Note: Logger is no longer templated on MinLevel to ensure all log macros
 * share the same thread_local queue instance.
 * MinLevel is now a compile-time constant defined by LOGZ_MIN_LEVEL.
 */
class Logger {
public:
    // Compile-time minimum log level
    static constexpr LogLevel MinLevel = LOGZ_MIN_LEVEL;

    /**
     * @brief Get the backend instance (global singleton)
     * @return Reference to Backend singleton
     */
    template<LogLevel BackendMinLevel = MinLevel>
    static Backend<BackendMinLevel>& get_backend() {
        return Backend<BackendMinLevel>::get_instance();
    }

    /**
     * @brief Get thread-local queue with automatic allocation from Backend
     * Each thread has its own queue, automatically allocated by Backend on first use
     * Backend owns the Queue, thread only borrows a pointer
     */
    static Queue& get_thread_queue();

    /**
     * @brief Log a message with variadic template parameters
     * @tparam Fmt Format string (compile-time constant)
     * @tparam Level Log level (compile-time constant)
     * @tparam Args Types of arguments to log
     * @param args Arguments to serialize and log
     * Note: Level check should be done at macro level before calling this function
     */
    template<auto Fmt, LogLevel Level, typename... Args>
    static void log_impl(const Args&... args);

private:
    /**
     * @brief Get current timestamp in nanoseconds
     */
    static uint64_t get_timestamp_ns() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
};

} // namespace logZ

// Backend must be included before implementing get_thread_queue
#include "Backend.h"

namespace logZ {

// Implementation of get_thread_queue() - must be after Backend is complete
inline Queue& Logger::get_thread_queue() {
    struct ThreadLocalData {
        Queue* queue_ptr;
        
        ThreadLocalData() {
            auto& backend = Logger::get_backend<MinLevel>();
            queue_ptr = backend.allocate_queue_for_thread();
            
            if (!queue_ptr) [[unlikely]] {
                throw std::runtime_error("Failed to allocate queue from Backend");
            }
        }
        
        ~ThreadLocalData() {
            if (queue_ptr) {
                auto& backend = Logger::get_backend<MinLevel>();
                backend.mark_queue_abandoned(queue_ptr);
            }
        }
    };
    
    static thread_local ThreadLocalData tls_data;
    
    return *tls_data.queue_ptr;
}

// Implementation of log_impl() - must be after Backend is complete
template<auto Fmt, LogLevel Level, typename... Args>
void Logger::log_impl(const Args&... args) {
    auto timestamp = get_timestamp_ns();
    // Calculate args size once
    size_t args_size = calculate_args_size(args...);
    size_t total_size = sizeof(Metadata) + args_size;

    // Reserve space in queue
    Queue& queue = get_thread_queue();
    std::byte* buffer = queue.reserve_write(total_size);
    // Hot path: Buffer allocation usually succeeds
    if (buffer == nullptr) [[unlikely]] {
        // Queue is full, log message lost
        // Increment dropped messages counter
        get_backend<MinLevel>().increment_dropped_count();
        return;
    }

    // Encode metadata and arguments into buffer using Encoder functions
    // Pass args_size to avoid redundant calculation
    encode_log_entry<Fmt, Level>(buffer, timestamp, args_size, args...);
    
    // Commit the write to make data visible to backend thread
    queue.commit_write(total_size);
}

} // namespace logZ

// Helper macros to extract first argument and remaining arguments
#define LOGZ_FIRST_ARG(fmt, ...) fmt
#define LOGZ_REST_ARGS(fmt, ...) __VA_ARGS__

// Logging macros - use Logger static methods
// Format: LOG_INFO("format string {}", arg1, arg2, ...)
// All macros use the same Logger class (no template parameter) to share the same thread_local queue
// Compile-time level check is done at macro expansion time
#define LOG_TRACE(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::TRACE >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::TRACE>(__VA_ARGS__); \
        } \
    } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::DEBUG >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::DEBUG>(__VA_ARGS__); \
        } \
    } while(0)

#define LOG_INFO(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::INFO >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::INFO>(__VA_ARGS__); \
        } \
    } while(0)

#define LOG_WARN(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::WARN >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::WARN>(__VA_ARGS__); \
        } \
    } while(0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::ERROR >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::ERROR>(__VA_ARGS__); \
        } \
    } while(0)

#define LOG_FATAL(fmt, ...) \
    do { \
        if constexpr (::logZ::LogLevel::FATAL >= ::logZ::Logger::MinLevel) { \
            ::logZ::Logger::log_impl<::logZ::FixedString(fmt), ::logZ::LogLevel::FATAL>(__VA_ARGS__); \
        } \
    } while(0)
