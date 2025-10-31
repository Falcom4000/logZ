#pragma once

#include <cstddef>
#include <cstdint>

namespace logZ {

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

// Forward declaration
class StringRingBuffer;

/**
 * @brief Decoder function pointer type (actual definition in Decoder.h)
 * Signature: void (*)(const std::byte*, StringRingBuffer::StringWriter&)
 */
using DecoderFunc = void (*)(const std::byte*, void*);

/**
 * @brief Log metadata stored at the beginning of each log entry
 */
struct Metadata {
    LogLevel level;
    uint64_t timestamp;      // Timestamp in nanoseconds
    uint32_t args_size;      // Size of arguments in bytes
    DecoderFunc decoder;     // Function pointer to decode the arguments (generated at compile-time)
};

} // namespace logZ
