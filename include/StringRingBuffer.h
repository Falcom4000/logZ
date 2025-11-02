#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <memory>

namespace logZ {

// Forward declaration
class Sinker;

/**
 * @brief Ring buffer for formatted string storage
 * Single-threaded, expandable ring buffer using std::byte as underlying storage
 * 
 * Simple byte stream storage - no delimiters, no length prefixes
 * Data is written in-place and read out as a byte stream
 */
class StringRingBuffer {
public:
    explicit StringRingBuffer(size_t initial_capacity = 64 * 1024)  // 64KB default
        : data_(new std::byte[initial_capacity]), capacity_(initial_capacity) {
    }

    ~StringRingBuffer() {
        delete[] data_;
    }

    // Disable copy and move
    StringRingBuffer(const StringRingBuffer&) = delete;
    StringRingBuffer& operator=(const StringRingBuffer&) = delete;
    StringRingBuffer(StringRingBuffer&&) = delete;
    StringRingBuffer& operator=(StringRingBuffer&&) = delete;

    /**
     * @brief Get a writer for in-place string construction
     * @return StringWriter object for building string in-place
     */
    class StringWriter {
    public:
        explicit StringWriter(StringRingBuffer* buffer) 
            : buffer_(buffer) {
        }

        ~StringWriter() = default;

        // Disable copy and move
        StringWriter(const StringWriter&) = delete;
        StringWriter& operator=(const StringWriter&) = delete;
        StringWriter(StringWriter&&) = delete;
        StringWriter& operator=(StringWriter&&) = delete;

        /**
         * @brief Append a string
         */
        void append(const std::string& str) {
            append(str.data(), str.size());
        }

        /**
         * @brief Append a string_view
         */
        void append(std::string_view sv) {
            append(sv.data(), sv.size());
        }

        /**
         * @brief Append a C-string
         */
        void append(const char* str) {
            append(str, std::strlen(str));
        }

        /**
         * @brief Append raw data
         */
        void append(const char* data, size_t length) {
            // Ensure we have space
            if (buffer_->get_free_space() < length) {
                if (!buffer_->expand(length)) {
                    return;  // Cannot expand
                }
            }
            
            buffer_->write_bytes(reinterpret_cast<const std::byte*>(data), length);
        }

        /**
         * @brief Append a single character (for output iterator compatibility)
         */
        void push_back(char c) {
            if (buffer_->get_free_space() < 1) {
                if (!buffer_->expand(1)) {
                    return;  // Cannot expand
                }
            }
            buffer_->write_bytes(reinterpret_cast<const std::byte*>(&c), 1);
        }

        /**
         * @brief Output iterator for std::format_to
         */
        class Iterator {
        public:
            using iterator_category = std::output_iterator_tag;
            using value_type = char;
            using difference_type = std::ptrdiff_t;
            using pointer = char*;
            using reference = char&;

            explicit Iterator(StringWriter* writer) : writer_(writer) {}

            Iterator& operator=(char c) {
                writer_->push_back(c);
                return *this;
            }

            Iterator& operator*() { return *this; }
            Iterator& operator++() { return *this; }
            Iterator operator++(int) { return *this; }

        private:
            StringWriter* writer_;
        };

        /**
         * @brief Get an output iterator for std::format_to
         */
        Iterator get_iterator() {
            return Iterator(this);
        }

    private:
        StringRingBuffer* buffer_;
    };

    /**
     * @brief Get a writer for in-place construction
     * @param sinker Optional sinker - if buffer is full, flush to sinker instead of expanding
     * @return StringWriter object, or throw if buffer is full and no sinker provided
     */
    StringWriter get_writer(class Sinker* sinker = nullptr) {
        size_t min_space = 256;  // Minimum space for string data
        if (get_free_space() < min_space) {
            if (sinker != nullptr) {
                // Flush to sinker instead of expanding
                flush_to_sinker(sinker);
            } else {
                // No sinker, expand as before
                expand(min_space);
            }
        }
        return StringWriter(this);
    }

    /**
     * @brief Read raw bytes from the buffer
     * @param out Buffer to read into
     * @param max_length Maximum bytes to read
     * @return Number of bytes actually read
     */
    size_t read(std::byte* out, size_t max_length) {
        size_t available = get_used_space();
        size_t to_read = (available < max_length) ? available : max_length;
        
        if (to_read == 0) {
            return 0;
        }
        
        read_bytes(out, to_read);
        return to_read;
    }

    /**
     * @brief Check if buffer is empty
     */
    bool empty() const {
        return read_ == write_;
    }

    /**
     * @brief Flush all data to sinker and clear buffer
     */
    void flush_to_sinker(Sinker* sinker);

private:
    /**
     * @brief Get used space in the buffer
     */
    size_t get_used_space() const {
        if (write_ >= read_) {
            return write_ - read_;
        } else {
            return capacity_ - read_ + write_;
        }
    }

    /**
     * @brief Get free space in the buffer
     */
    size_t get_free_space() const {
        return capacity_ - get_used_space() - 1;  // -1 to distinguish full from empty
    }

    /**
     * @brief Write bytes to the buffer
     */
    void write_bytes(const std::byte* src, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            data_[write_] = src[i];
            write_ = (write_ + 1) % capacity_;
        }
    }

    /**
     * @brief Read bytes from the buffer
     */
    void read_bytes(std::byte* dst, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            dst[i] = data_[read_];
            read_ = (read_ + 1) % capacity_;
        }
    }

    /**
     * @brief Expand the buffer capacity
     * @param min_additional Minimum additional space needed
     * @return true if successful, false if failed
     */
    bool expand(size_t min_additional) {
        size_t new_capacity = capacity_ * 2;
        while (new_capacity - capacity_ < min_additional) {
            new_capacity *= 2;
        }
        
        std::byte* new_data = new std::byte[new_capacity];
        
        // Copy existing data to new buffer
        size_t used = get_used_space();
        
        if (write_ >= read_) {
            // Contiguous data
            std::memcpy(new_data, data_ + read_, used);
        } else {
            // Wrapped data
            size_t first_part = capacity_ - read_;
            std::memcpy(new_data, data_ + read_, first_part);
            std::memcpy(new_data + first_part, data_, write_);
        }
        
        delete[] data_;
        data_ = new_data;
        capacity_ = new_capacity;
        read_ = 0;
        write_ = used;
        
        return true;
    }

    std::byte* data_;      // Underlying byte buffer
    size_t capacity_;      // Total capacity in bytes
    size_t read_{0};       // Read position
    size_t write_{0};      // Write position
};

} // namespace logZ

// Include Sinker after StringRingBuffer definition and outside namespace
#include "Sinker.h"

namespace logZ {

// Inline implementation of flush_to_sinker
inline void StringRingBuffer::flush_to_sinker(Sinker* sinker) {
    if (sinker == nullptr || empty()) {
        return;
    }

    size_t used = get_used_space();
    
    if (write_ >= read_) {
        // Data is contiguous
        sinker->write(data_ + read_, used);
    } else {
        // Data wraps around
        // First part: from read_ to end of buffer
        size_t first_part = capacity_ - read_;
        sinker->write(data_ + read_, first_part);
        
        // Second part: from start to write_
        sinker->write(data_, write_);
    }
    
    // Flush to ensure data is written to disk
    sinker->flush();
    
    // Clear the buffer after flushing
    read_ = 0;
    write_ = 0;
}

} // namespace logZ
