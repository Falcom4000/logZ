#pragma once
#include "LogTypes.h"
#include "StringRingBuffer.h"
#include "Fixedstring.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <tuple>
#include <utility>
#include <format>
#include <string>
#include <string_view>


namespace logZ {

/**
 * @brief Decoder function type
 * Takes a buffer pointer and a writer, writes directly to writer
 * 
 * @param ptr Pointer to encoded arguments data
 * @param writer StringWriter to write formatted output (passed as void* from LogTypes.h)
 * 
 * Note: DecoderFunc is defined in LogTypes.h as void (*)(const std::byte*, void*)
 * to avoid circular dependencies. The actual signature we use is:
 * void (*)(const std::byte*, StringRingBuffer::StringWriter&)
 */


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
        // Case 1: Arithmetic types (int, double, enum, etc.) - direct memory read
        if constexpr (std::is_arithmetic_v<RawT> || std::is_enum_v<RawT>) {
            RawT value;
            std::memcpy(&value, ptr, sizeof(RawT));
            return std::make_pair(value, ptr + sizeof(RawT));
        }
        // Case 2: Compile-time string literals (FixedString)
        // Stored as length (2 bytes) + pointer (8 bytes)
        else if constexpr (is_fixed_string_v<RawT>) {
            unsigned short len = 0;
            std::memcpy(&len, ptr, sizeof(unsigned short));
            ptr += sizeof(unsigned short);
            
            const char* str_ptr = nullptr;
            std::memcpy(&str_ptr, ptr, sizeof(const char*));
            ptr += sizeof(const char*);
            
            // Return string_view pointing to the literal
            return std::make_pair(std::string_view(str_ptr, len), ptr);
        }
        // Case 3: Runtime strings (char[], std::string, std::string_view, const char*)
        // Stored as length (2 bytes) + content
        else if constexpr (
            (std::is_array_v<RawT> && std::is_same_v<std::remove_extent_t<RawT>, char>) ||
            std::is_same_v<RawT, std::string> ||
            std::is_same_v<RawT, std::string_view> ||
            (std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>)
        ) {
            unsigned short len = 0;
            std::memcpy(&len, ptr, sizeof(unsigned short));
            ptr += sizeof(unsigned short);
            
            const char* str = reinterpret_cast<const char*>(ptr);
            return std::make_pair(std::string_view(str, len), ptr + len);
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
 * @tparam FMT Format string as non-type template parameter
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
        // Decode all format arguments one by one
        // Use std::tuple{...} instead of std::make_tuple(...) to ensure 
        // left-to-right evaluation order of the initializer list
        auto args_tuple = std::tuple{
            [&current]() {
                auto pair = DecodedValue<Args>::decode_impl(current);
                current = pair.second;  // Update pointer after each decode
                return pair.first;       // Return the decoded value
            }()...
        };
        
        // Format and write directly to writer using format_to
        std::apply([&](auto&&... args) {
            // Use std::format_to to write directly to the ring buffer
            // No temporary string allocation
            std::format_to_n(writer.get_iterator(),
                             writer.get_free_space(),
                             FMT.sv(),
                             std::forward<decltype(args)>(args)...);
        }, args_tuple);
    }
}

/**
 * @brief Generate decoder function for specific argument types
 * @tparam FMT Format string as non-type template parameter
 * @tparam Args Types of format arguments (remaining arguments)
 * @return Function pointer that can decode these argument types and write to writer
 */
template<auto FMT, typename... Args>
auto get_decoder() {
    // Return a static function pointer for this specific argument type combination
    // This function is generated at compile-time, one per unique Args... combination
    return &decode<FMT, Args...>;
}

} // namespace logZ
