#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <x86intrin.h>

namespace logZ {

// ════════════════════════════════════════════════════════
// TSC (Time Stamp Counter) 校准 - 用于超低延迟时间戳
// ════════════════════════════════════════════════════════

/**
 * @brief TSC 频率校准数据
 * 在程序启动时校准一次，用于将 TSC 转换为纳秒
 */
struct TscCalibration {
    uint64_t tsc_start;
    uint64_t ns_start;
    double tsc_to_ns_ratio;  // 1 TSC tick = ratio nanoseconds
    
    static TscCalibration& instance() {
        static TscCalibration cal = calibrate();
        return cal;
    }
    
private:
    static TscCalibration calibrate() {
        TscCalibration cal;
        
        // 获取起始点
        auto start_time = std::chrono::steady_clock::now();
        uint64_t start_tsc = __rdtsc();
        
        // 等待一小段时间来校准
        volatile int dummy = 0;
        for (int i = 0; i < 1000000; ++i) {
            dummy += i;
        }
        
        // 获取结束点
        auto end_time = std::chrono::steady_clock::now();
        uint64_t end_tsc = __rdtsc();
        
        // 计算比率
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        uint64_t elapsed_tsc = end_tsc - start_tsc;
        
        cal.tsc_to_ns_ratio = static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_tsc);
        
        // 记录基准点
        cal.ns_start = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        cal.tsc_start = __rdtsc();
        
        return cal;
    }
};

/**
 * @brief 将 TSC 值转换为纳秒时间戳
 */
__attribute__((always_inline))
inline uint64_t tsc_to_ns(uint64_t tsc) {
    auto& cal = TscCalibration::instance();
    int64_t tsc_diff = static_cast<int64_t>(tsc - cal.tsc_start);
    return cal.ns_start + static_cast<uint64_t>(tsc_diff * cal.tsc_to_ns_ratio);
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

// Forward declaration
class StringRingBuffer;

/**
 * @brief Decoder function pointer type (actual definition in Decoder.h)
 * Signature: void (*)(const std::byte*, StringRingBuffer::StringWriter&)
 */
using DecoderFunc = void (*)(const std::byte*, void*);

/**
 * @brief Log metadata stored at the beginning of each log entry
 * 
 * 优化后的内存布局（24 bytes，无 padding）：
 * - timestamp: 8 bytes (offset 0)
 * - decoder:   8 bytes (offset 8)
 * - args_size: 4 bytes (offset 16)
 * - level:     1 byte  (offset 20)
 * - padding:   3 bytes (offset 21-23)
 * 
 * 原布局需要 32 bytes，优化后只需 24 bytes
 */
struct Metadata {
    uint64_t timestamp;      // TSC value from RDTSC (8 bytes)
    DecoderFunc decoder;     // Function pointer (8 bytes)
    uint32_t args_size;      // Size of arguments in bytes (4 bytes)
    LogLevel level;          // Log level (1 byte)
    // 3 bytes padding to align to 8 bytes
};

} // namespace logZ
