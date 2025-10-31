#pragma once
#include "StringRingBuffer.h"
#include "Fixedstring.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <tuple>
#include <utility>
#include <format>


namespace logZ {

/**
 * @brief Decoder function type
 * Takes a buffer pointer and a writer, writes directly to writer
 * 
 * @param ptr Pointer to encoded arguments data
 * @param writer StringWriter to write formatted output
 */
using DecoderFunc = void (*)(const std::byte*, StringRingBuffer::StringWriter&);

/**
 * @brief Helper to decode and extract value from a single argument
 * @tparam T Type to decode
 * @param ptr Buffer pointer to read from
 * @return Tuple of (decoded value, next pointer position)
 */
template<typename T>
struct DecodedValue {
    using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
    
    static auto decode_impl(const std::byte* ptr) {
        // Case 1: POD types (int, double, etc.) - direct memory read
        if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
            RawT value;
            std::memcpy(&value, ptr, sizeof(RawT));
            return std::make_pair(value, ptr + sizeof(RawT));
        }
        // Case 2: Compile-time string literals - read pointer (8 bytes) + length (2 bytes unsigned short)
        else if constexpr (std::is_array_v<RawT> && 
                          std::is_same_v<std::remove_extent_t<RawT>, char>) {
            const char* str_ptr = nullptr;
            std::memcpy(&str_ptr, ptr, sizeof(const char*));
            ptr += sizeof(const char*);
            
            unsigned short len = 0;
            std::memcpy(&len, ptr, sizeof(unsigned short));
            ptr += sizeof(unsigned short);
            
            return std::make_pair(str_ptr, ptr);
        }
        // Case 3: Runtime strings - read string content
        else if constexpr (std::is_pointer_v<RawT> && 
                          std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>) {
            const char* str = reinterpret_cast<const char*>(ptr);
            size_t len = std::strlen(str) + 1;
            return std::make_pair(str, ptr + len);
        }
        // Default case for other types
        else {
            RawT value;
            std::memcpy(&value, ptr, sizeof(RawT));
            return std::make_pair(value, ptr + sizeof(RawT));
        }
    }
};


/**
 * @brief Decoder implementation for specific argument types (compile-time generated)
 * This function is instantiated at compile-time for each unique Args... combination
 * It can be called at runtime from any thread (e.g., consumer thread)
 * 
 * The first argument is expected to be a format string, and the rest are the format arguments.
 * 
 * @tparam Args Types of arguments to decode (first one should be format string)
 * @param ptr Pointer to encoded arguments
 * @param writer StringWriter to write formatted output
 */
template<auto FMT, typename... Args>
void decode(const std::byte* ptr, StringRingBuffer::StringWriter& writer) {
    const std::byte* current = ptr;
    
    if constexpr (sizeof...(Args) == 0) {
        // No format arguments, just write the format string
        writer.append(FMT.sv());
    } else {
        // Decode all format arguments
        auto decode_all_args = [&current]() {
            return std::make_tuple(
                [&current]() {
                    auto pair = DecodedValue<Args>::decode_impl(current);
                    current = pair.second;
                    return pair.first;
                }()...
            );
        };
        
        auto args_tuple = decode_all_args();
        
        // Format and write directly to writer
        std::apply([&](auto&&... args) {
            // Use std::format to create the string, then write once
            // TODO: no copy nor memory allocation version
            auto formatted = std::format(FMT.sv(), std::forward<decltype(args)>(args)...);
            writer.append(formatted);
        }, args_tuple);
    }
}

/**
 * @brief Generate decoder function for specific argument types
 * @tparam FormatStr Type of format string (first argument)
 * @tparam Args Types of format arguments (remaining arguments)
 * @return Function pointer that can decode these argument types and write to writer
 */
template<auto FMT, typename... Args>
auto get_decoder() {
    // Return a static function pointer for this specific argument type combination
    // This function is generated at compile-time, one per unique Args... combination
    return &decode<FixedString(FMT), Args...>;
}

} // namespace logZ
