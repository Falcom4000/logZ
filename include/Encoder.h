#pragma once

#include "LogTypes.h"
#include "Decoder.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string>
#include <string_view>

namespace logZ {

/**
 * @brief Encoder utilities for serializing log arguments into buffer
 * This file contains all encoding-related functions for the logging system
 */

/**
 * @brief Helper to calculate size for a single argument
 */
template<typename T>
inline size_t calculate_single_arg_size(const T& arg) {
    using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
    
    // Case 1: POD types (int, double, etc.) - use sizeof
    if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
        return sizeof(RawT);
    }
    // Case 2: Compile-time string literals (const char[N] or const char(&)[N])
    // Must check BEFORE decay to distinguish from char*
    else if constexpr (std::is_array_v<RawT> && 
                      std::is_same_v<std::remove_extent_t<RawT>, char>) {
        // String literal: store as length (2 bytes unsigned short) + pointer (8 bytes)
        return sizeof(unsigned short) + 8;
    }
    // Case 3: std::string - store length (2 bytes) + content (no '\0' needed)
    else if constexpr (std::is_same_v<RawT, std::string>) {
        return sizeof(unsigned short) + arg.size(); // length + content
    }
    // Case 4: std::string_view - store length (2 bytes) + content (no '\0' needed)
    else if constexpr (std::is_same_v<RawT, std::string_view>) {
        return sizeof(unsigned short) + arg.size(); // length + content
    }
    // Case 5: Runtime strings (const char* or char*)
    else if constexpr (std::is_pointer_v<RawT> && 
                      std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>) {
        // Runtime string: store length (2 bytes) + content (no '\0' needed)
        if (arg != nullptr) {
            size_t len = std::strlen(arg);
            return sizeof(unsigned short) + len;
        }
        return 0;
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
inline size_t calculate_args_size(const Args&... args) {
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
inline std::byte* encode_single_arg(std::byte* ptr, const T& arg) {
    using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
    
    // Case 1: POD types (int, double, etc.) - direct memory copy
    if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
        std::memcpy(ptr, &arg, sizeof(RawT));
        return ptr + sizeof(RawT);
    }
    // Case 2: Compile-time string literals - store length (2 bytes) + pointer (8 bytes)
    else if constexpr (std::is_array_v<RawT> && 
                      std::is_same_v<std::remove_extent_t<RawT>, char>) {
        // Store the length as unsigned short first
        unsigned short len = static_cast<unsigned short>(std::extent_v<RawT> - 1); // -1 to exclude '\0'
        std::memcpy(ptr, &len, sizeof(unsigned short));
        ptr += sizeof(unsigned short);
        
        // Store the pointer to the string literal
        const char* str_ptr = arg;
        std::memcpy(ptr, &str_ptr, sizeof(const char*));
        return ptr + sizeof(const char*);
    }
    // Case 3: std::string - store length (2 bytes) + content (no '\0' needed)
    else if constexpr (std::is_same_v<RawT, std::string>) {
        unsigned short len = static_cast<unsigned short>(arg.size());
        std::memcpy(ptr, &len, sizeof(unsigned short));
        ptr += sizeof(unsigned short);
        std::memcpy(ptr, arg.data(), len); // No '\0' needed
        return ptr + len;
    }
    // Case 4: std::string_view - store length (2 bytes) + content (no '\0' needed)
    else if constexpr (std::is_same_v<RawT, std::string_view>) {
        unsigned short len = static_cast<unsigned short>(arg.size());
        std::memcpy(ptr, &len, sizeof(unsigned short));
        ptr += sizeof(unsigned short);
        std::memcpy(ptr, arg.data(), len); // No '\0' needed
        return ptr + len;
    }
    // Case 5: Runtime C strings (const char* or char*) - store length (2 bytes) + content (no '\0' needed)
    else if constexpr (std::is_pointer_v<RawT> && 
                      std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>) {
        if (arg != nullptr) {
            size_t str_len = std::strlen(arg);
            unsigned short len = static_cast<unsigned short>(str_len);
            std::memcpy(ptr, &len, sizeof(unsigned short));
            ptr += sizeof(unsigned short);
            std::memcpy(ptr, arg, len); // No '\0' needed
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
inline void encode_args(std::byte* buffer, const Args&... args) {
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
 * @tparam FMT Format string (compile-time constant)
 * @tparam Level Log level (compile-time constant)
 * @tparam Args Types of arguments to encode
 * @param buffer Buffer to write to
 * @param timestamp Timestamp in nanoseconds
 * @param args_size Size of arguments (pre-calculated to avoid redundant computation)
 * @param args Arguments to encode
 */
template<auto FMT, LogLevel Level, typename... Args>
inline void encode_log_entry(std::byte* buffer, uint64_t timestamp, size_t args_size, const Args&... args) {
    // Use Metadata from LogTypes.h
    Metadata* metadata = reinterpret_cast<Metadata*>(buffer);
    metadata->level = Level;
    metadata->timestamp = timestamp;
    metadata->args_size = static_cast<uint32_t>(args_size);
    // Cast decoder function pointer to DecoderFunc type (void (*)(const std::byte*, void*))
    metadata->decoder = reinterpret_cast<DecoderFunc>(get_decoder<FMT, Args...>());
    
    // Write arguments after metadata
    std::byte* ptr = buffer + sizeof(Metadata);
    encode_args(ptr, args...);
}

} // namespace logZ
