#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace logZ {

/**
 * @brief Lock-free ring buffer for bytes, supporting single producer single consumer (SPSC)
 * 
 * This is a fixed-size circular buffer that stores std::byte data.
 * It supports lock-free operations for one writer and one reader.
 * 
 * Cache line alignment: Hot data members are aligned to prevent false sharing
 */
class alignas(64) RingBytes {
public:
    /**
     * @brief Constructor
     * @param capacity The capacity of the ring buffer (must be power of 2 for better performance)
     */
    explicit RingBytes(size_t capacity)
        : capacity_(next_power_of_2(capacity))
        , capacity_mask_(capacity_ - 1)
        , write_pos_(0)
        , read_pos_(0)
        , buffer_(std::make_unique<std::byte[]>(capacity_)) {
        
        // 优化：预先触发page fault，避免运行时延迟尖刺
        // 写入每个4KB页面（4096字节）让内核分配物理内存
        // 这会在初始化时一次性完成所有page fault，而不是在日志写入时触发
        constexpr size_t page_size = 4096;
        for (size_t i = 0; i < capacity_; i += page_size) {
            buffer_[i] = std::byte{0};
        }
    }

    ~RingBytes() = default;

    // Disable copy and move
    RingBytes(const RingBytes&) = delete;
    RingBytes& operator=(const RingBytes&) = delete;
    RingBytes(RingBytes&&) = delete;
    RingBytes& operator=(RingBytes&&) = delete;

    /**
     * @brief Reserve space for writing
     * @param size The number of bytes to reserve
     * @return Pointer to the reserved space, or nullptr if not enough space
     * 
     * This function reserves space in the buffer for writing. The actual write
     * should be done by the caller, followed by a call to commit_write().
     * 
     * NOTE: This implementation does NOT handle wrap-around writes.
     * If a write would cross the buffer boundary, nullptr is returned.
     * The caller (Queue) should handle this by allocating a new node.
     */
    std::byte* reserve_write(size_t size) {
        if (size == 0 || size > capacity_) {
            return nullptr;
        }

        // Load current positions
        uint64_t current_write = write_pos_.load(std::memory_order_relaxed);
        uint64_t current_read = read_pos_.load(std::memory_order_acquire);

        // Calculate available space
        uint64_t available = capacity_ - (current_write - current_read);
        
        // Hot path: Usually have space
        if (size > available) [[unlikely]] {
            return nullptr;  // Not enough space
        }

        // Calculate position in buffer using bit mask (faster than modulo)
        size_t pos = current_write & capacity_mask_;
        
        // CRITICAL: Check if write would wrap around buffer boundary
        // If so, reject this write (caller should use new node)
        // Hot path: Usually doesn't wrap
        if (pos + size > capacity_) [[unlikely]] {
            return nullptr;  // Would wrap around, need new node
        }
        write_pos_.store(current_write + size, std::memory_order_release);

        // Return pointer to the reserved space
        return &buffer_[pos];
    }

    /**
     * @brief Write data to the buffer
     * @param data Pointer to the data to write
     * @param size Number of bytes to write
     * @return Pointer to where the data was written, or nullptr if not enough space
     * 
     * This is a convenience function that combines reserve and memcpy.
     * Still needs commit_write() to make the data visible to readers.
     */
    std::byte* write(const void* data, size_t size) {
        std::byte* dest = reserve_write(size);
        if (dest == nullptr) [[unlikely]] {
            return nullptr;
        }

        // Handle wrap-around case using bit mask (faster than modulo)
        size_t write_start = (write_pos_.load(std::memory_order_relaxed) - size) & capacity_mask_;
        size_t first_part = std::min(size, capacity_ - write_start);
        
        std::memcpy(&buffer_[write_start], data, first_part);
        
        if (first_part < size) {
            // Wrap around to the beginning
            std::memcpy(&buffer_[0], 
                       static_cast<const std::byte*>(data) + first_part,
                       size - first_part);
        }

        return &buffer_[write_start];
    }

    /**
     * @brief Read data from the buffer
     * @param size Number of bytes to read
     * @return Pointer to the data, or nullptr if not enough data available
     * 
     * The returned pointer is valid until the next call to commit_read().
     * The caller should copy the data if needed before calling commit_read().
     */
    std::byte* read(size_t size) {
        if (size == 0) {
            return nullptr;
        }

        uint64_t current_read = read_pos_.load(std::memory_order_relaxed);
        uint64_t current_write = write_pos_.load(std::memory_order_acquire);

        // Calculate available data
        uint64_t available = current_write - current_read;
        
        if (size > available) {
            return nullptr;  // Not enough data
        }

        // Return pointer to the data using bit mask (faster than modulo)
        size_t pos = current_read & capacity_mask_;
        return &buffer_[pos];
    }

    /**
     * @brief Commit the read operation
     * @param size Number of bytes to commit as read
     * 
     * This advances the read position and frees up space for writing.
     */
    void commit_read(size_t size) {
        uint64_t current_read = read_pos_.load(std::memory_order_relaxed);
        read_pos_.store(current_read + size, std::memory_order_release);
    }

    /**
     * @brief Get the capacity of the buffer
     * @return Capacity in bytes
     */
    size_t capacity() const {
        return capacity_;
    }

    /**
     * @brief Get the number of bytes available for reading
     * @return Number of bytes available
     */
    size_t available_read() const {
        uint64_t current_read = read_pos_.load(std::memory_order_relaxed);
        uint64_t current_write = write_pos_.load(std::memory_order_acquire);
        return current_write - current_read;
    }

    /**
     * @brief Get the number of bytes available for writing
     * @return Number of bytes available
     */
    size_t available_write() const {
        uint64_t current_write = write_pos_.load(std::memory_order_relaxed);
        uint64_t current_read = read_pos_.load(std::memory_order_acquire);
        return capacity_ - (current_write - current_read);
    }

private:
    /**
     * @brief Round up to the next power of 2
     * @param n Input value
     * @return Next power of 2 >= n
     */
    static constexpr size_t next_power_of_2(size_t n) {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    const size_t capacity_;                    // Fixed capacity of the buffer (power of 2)
    const size_t capacity_mask_;               // Bit mask for fast modulo (capacity - 1)
    alignas(64) std::atomic<uint64_t> write_pos_;          // Current write position (优化: 合并了 write_commit_pos_)
    alignas(64) std::atomic<uint64_t> read_pos_;           // Current read position
    std::unique_ptr<std::byte[]> buffer_;      // The actual buffer
};

}  // namespace logZ
