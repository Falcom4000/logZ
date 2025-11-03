#pragma once

#include "LogTypes.h"
#include "Decoder.h"
#include "Fixedstring.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string>
#include <string_view>

namespace logZ {

template<typename T>
inline size_t calculate_single_arg_size(const T& arg) {
    using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
    
    // Case 1: Arithmetic types (int, double, enum, etc.) - direct sizeof
    if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
        return sizeof(RawT);
    }
    // Case 2: Compile-time string literals (FixedString)
    // Store as length (2 bytes) + pointer (8 bytes)
    // SAFE: FixedString guarantees the data is a compile-time literal
    else if constexpr (is_fixed_string_v<RawT>) {
        return sizeof(unsigned short) + sizeof(const char*);
    }
    // Case 3: Runtime strings (char[], std::string, std::string_view, const char*)
    // Store as length (2 bytes) + content (for safety)
    else if constexpr (
        (std::is_array_v<RawT> && std::is_same_v<std::remove_extent_t<RawT>, char>) ||
        std::is_same_v<RawT, std::string> ||
        std::is_same_v<RawT, std::string_view> ||
        (std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>)
    ) {
        // Calculate actual string length
        unsigned short len = 0;
        if constexpr (std::is_same_v<RawT, std::string> || std::is_same_v<RawT, std::string_view>) {
            len = arg.size();
        } else if constexpr (std::is_pointer_v<RawT>) {
            len = (arg != nullptr) ? std::strlen(arg) : 0;
        } else {
            // char array
            len = std::strlen(arg);
        }
        return sizeof(unsigned short) + len;
    }
    // Default case for other types
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
    
    // Case 1: Arithmetic types (int, double, enum, etc.) - direct memory copy
    if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
        std::memcpy(ptr, &arg, sizeof(RawT));
        return ptr + sizeof(RawT);
    }
    // Case 2: Compile-time string literals (FixedString)
    // Store length (2 bytes) + pointer (8 bytes)
    // SAFE: FixedString guarantees the data is a compile-time literal
    else if constexpr (is_fixed_string_v<RawT>) {
        auto sv = arg.sv();
        unsigned short len = static_cast<unsigned short>(sv.size());
        std::memcpy(ptr, &len, sizeof(unsigned short));
        ptr += sizeof(unsigned short);
        
        // Store pointer to the literal stored in FixedString
        const char* str_ptr = sv.data();
        std::memcpy(ptr, &str_ptr, sizeof(const char*));
        return ptr + sizeof(const char*);
    }
    // Case 3: Runtime strings (char[], std::string, std::string_view, const char*)
    // Store length (2 bytes) + content (for safety)
    else if constexpr (
        (std::is_array_v<RawT> && std::is_same_v<std::remove_extent_t<RawT>, char>) ||
        std::is_same_v<RawT, std::string> ||
        std::is_same_v<RawT, std::string_view> ||
        (std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>)
    ) {
        // Get string data and length
        const char* str_data = nullptr;
        size_t str_len = 0;
        
        if constexpr (std::is_same_v<RawT, std::string>) {
            str_data = arg.data();
            str_len = arg.size();
        } else if constexpr (std::is_same_v<RawT, std::string_view>) {
            str_data = arg.data();
            str_len = arg.size();
        } else if constexpr (std::is_pointer_v<RawT>) {
            if (arg == nullptr) {
                return ptr;  // Skip nullptr
            }
            str_data = arg;
            str_len = std::strlen(arg);
        } else {
            // char array
            str_data = arg;
            str_len = std::strlen(arg);
        }
        
        // Write length + content
        unsigned short len = static_cast<unsigned short>(str_len);
        std::memcpy(ptr, &len, sizeof(unsigned short));
        ptr += sizeof(unsigned short);
        std::memcpy(ptr, str_data, len);
        return ptr + len;
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
