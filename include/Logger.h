#pragma once

#include "Queue.h"
#include "Decoder.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <memory>
#include <type_traits>

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
    uint32_t args_size;      // Size of arguments in bytes
    DecoderFunc decoder;     // Function pointer to decode the arguments (generated at compile-time)
};

/**
 * @brief Logger frontend - puts messages into queue
 * @tparam MinLevel Minimum log level (compile-time constant)
 */
template<LogLevel MinLevel = LogLevel::INFO>
class Logger {
public:
    /**
     * @brief Get thread-local queue
     * Each thread has its own queue
     */
    static Queue& get_thread_queue() {
        static thread_local Queue queue(4096);
        return queue;
    }

    /**
     * @brief Log a message with variadic template parameters
     * @tparam Level Log level (compile-time constant)
     * @tparam Args Types of arguments to log
     * @param args Arguments to serialize and log
     */
    template<LogLevel Level, typename... Args>
    static void log_impl(const Args&... args) {
        auto timestamp = get_timestamp_ns();
        // Calculate total size needed
        size_t total_size = sizeof(LogMetadata) + calculate_args_size(args...);

        // Reserve space in queue
        Queue& queue = get_thread_queue();
        std::byte* buffer = queue.reserve_write(total_size);
        if (buffer == nullptr) {
            // Queue is full, log message lost
            // TODO: Add metrics for dropped messages
            return;
        }

        // Encode metadata and arguments into buffer
        encode<Level>(buffer, timestamp, args...);

        // Commit write
        queue.commit_write(total_size);
    }

private:
    /**
     * @brief Helper to calculate size for a single argument
     */
    template<typename T>
    static size_t calculate_single_arg_size(const T& arg) {
        using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
        
        // Case 1: POD types (int, double, etc.) - use sizeof
        if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
            return sizeof(RawT);
        }
        // Case 2: Compile-time string literals (const char[N] or const char(&)[N])
        // Must check BEFORE decay to distinguish from char*
        else if constexpr (std::is_array_v<RawT> && 
                          std::is_same_v<std::remove_extent_t<RawT>, char>) {
            // String literal: store as pointer (8 bytes) + length (2 bytes unsigned short)
            return 8 + sizeof(unsigned short);
        }
        // Case 3: Runtime strings (const char* or char*)
        else if constexpr (std::is_pointer_v<RawT> && 
                          std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>) {
            // Runtime string: store actual string content including '\0'
            return arg != nullptr ? (std::strlen(arg) + 1) : 0;
        }
        // Default case for other types (could be extended)
        else {
            return sizeof(RawT);
        }
    }

    /**
     * @brief Calculate the size needed for log arguments
     */
    template<typename... Args>
    static size_t calculate_args_size(const Args&... args) {
        if constexpr (sizeof...(args) == 0) {
            return 0;
        } else {
            // Fold expression to sum sizes of all arguments
            return (calculate_single_arg_size(args) + ...);
        }
    }

    /**
     * @brief Helper to encode a single argument into buffer
     * @return Pointer to the next write position after encoding
     */
    template<typename T>
    static std::byte* encode_single_arg(std::byte* ptr, const T& arg) {
        using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
        
        // Case 1: POD types (int, double, etc.) - direct memory copy
        if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
            std::memcpy(ptr, &arg, sizeof(RawT));
            return ptr + sizeof(RawT);
        }
        // Case 2: Compile-time string literals - store pointer (8 bytes) + length (2 bytes)
        else if constexpr (std::is_array_v<RawT> && 
                          std::is_same_v<std::remove_extent_t<RawT>, char>) {
            // Store the pointer to the string literal
            const char* str_ptr = arg;
            std::memcpy(ptr, &str_ptr, sizeof(const char*));
            ptr += sizeof(const char*);
            
            // Store the length as unsigned short
            unsigned short len = static_cast<unsigned short>(std::extent_v<RawT> - 1); // -1 to exclude '\0'
            std::memcpy(ptr, &len, sizeof(unsigned short));
            return ptr + sizeof(unsigned short);
        }
        // Case 3: Runtime strings - store full string content with '\0'
        else if constexpr (std::is_pointer_v<RawT> && 
                          std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>) {
            if (arg != nullptr) {
                size_t len = std::strlen(arg) + 1; // Include '\0'
                std::memcpy(ptr, arg, len);
                return ptr + len;
            }
            return ptr;
        }
        // Default case for other types
        else {
            std::memcpy(ptr, &arg, sizeof(RawT));
            return ptr + sizeof(RawT);
        }
    }

    /**
     * @brief Serialize arguments into buffer
     */
    template<typename... Args>
    static void encode_args(std::byte* buffer, const Args&... args) {
        if constexpr (sizeof...(args) == 0) {
            return;
        } else {
            // Fold expression to encode all arguments sequentially
            std::byte* ptr = buffer;
            ((ptr = encode_single_arg(ptr, args)), ...);
        }
    }

    /**
     * @brief Encode log metadata and arguments into buffer
     * @tparam Level Log level
     * @tparam Args Types of arguments to encode
     * @param buffer Buffer to write to
     * @param timestamp Timestamp in nanoseconds
     * @param args Arguments to encode
     * @return Pointer to the end of encoded data
     */
    template<LogLevel Level, typename... Args>
    static void encode(std::byte* buffer, uint64_t timestamp, const Args&... args) {
        // Calculate args size
        size_t args_size = calculate_args_size(args...);
        
        // Write metadata
        LogMetadata* metadata = reinterpret_cast<LogMetadata*>(buffer);
        metadata->level = Level;
        metadata->timestamp = timestamp;
        metadata->args_size = static_cast<uint32_t>(args_size);
        metadata->decoder = get_decoder<Args...>();  // Store decoder function pointer
        
        // Write arguments after metadata
        std::byte* ptr = buffer + sizeof(LogMetadata);
        encode_args(ptr, args...);
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
};

} // namespace logZ

// Logging macros - use Logger static methods
#define LOG_TRACE(...) \
    do { \
        ::logZ::Logger<::logZ::LogLevel::TRACE>::log_impl<::logZ::LogLevel::TRACE>(__VA_ARGS__); \
    } while(0)

#define LOG_DEBUG(...) \
    do { \
        ::logZ::Logger<::logZ::LogLevel::DEBUG>::log_impl<::logZ::LogLevel::DEBUG>(__VA_ARGS__); \
    } while(0)

#define LOG_INFO(...) \
    do { \
        ::logZ::Logger<>::log_impl<::logZ::LogLevel::INFO>(__VA_ARGS__); \
    } while(0)

#define LOG_WARN(...) \
    do { \
        ::logZ::Logger<>::log_impl<::logZ::LogLevel::WARN>(__VA_ARGS__); \
    } while(0)

#define LOG_ERROR(...) \
    do { \
        ::logZ::Logger<>::log_impl<::logZ::LogLevel::ERROR>(__VA_ARGS__); \
    } while(0)

#define LOG_FATAL(...) \
    do { \
        ::logZ::Logger<>::log_impl<::logZ::LogLevel::FATAL>(__VA_ARGS__); \
    } while(0)
