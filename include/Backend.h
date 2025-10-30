#pragma once

#include "Decoder.h"
#include "Logger.h"
#include "Queue.h"
#include "StringRingBuffer.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace logZ {

/**
 * @brief Backend logger that consumes from all thread queues
 * Runs in a single thread, no thread-safety needed for internal operations
 */
template<LogLevel MinLevel = LogLevel::INFO>
class Backend {
public:
    Backend() : running_(false), output_buffer_(1024) {}

    ~Backend() {
        stop();
    }
    
    // Disable copy and move
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend&&) = delete;

    /**
     * @brief Register a thread's queue for consumption
     * Should be called from each logging thread
     */
    void register_queue(Queue* queue) {
        queues_.push_back(queue);
    }

    /**
     * @brief Start the backend consumer thread
     */
    void start() {
        if (running_.exchange(true)) {
            return; // Already running
        }

        consumer_thread_ = std::thread([this]() {
            this->consume_loop();
        });
    }

    /**
     * @brief Stop the backend consumer thread
     */
    void stop() {
        if (!running_.exchange(false)) {
            return; // Not running
        }

        if (consumer_thread_.joinable()) {
            consumer_thread_.join();
        }
    }

    /**
     * @brief Read raw bytes from output buffer
     * @param out Buffer to write to
     * @param max_length Maximum bytes to read
     * @return Number of bytes actually read
     */
    size_t read_output(std::byte* out, size_t max_length) {
        return output_buffer_.read(out, max_length);
    }

    /**
     * @brief Check if output buffer is empty
     */
    bool output_empty() const {
        return output_buffer_.empty();
    }

private:
    /**
     * @brief Main consume loop running in backend thread
     */
    void consume_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            bool processed_any = process_one_log();

            // If no work was done, sleep briefly to avoid busy-waiting
            if (!processed_any) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // Final drain of all queues in timestamp order
        while (process_one_log()) {
            // Keep processing until all queues are empty
        }
    }

    /**
     * @brief Process one log entry with minimum timestamp from all queues
     * @return true if a log was processed, false if all queues are empty
     */
    bool process_one_log() {
        // Poll all registered queues and find the log entry with minimum timestamp
        Queue* selected_queue = nullptr;
        const LogMetadata* selected_meta = nullptr;
        uint64_t min_timestamp = UINT64_MAX;
        
        // Traverse all queue heads to find minimum timestamp
        for (Queue* queue : queues_) {
            if (queue != nullptr) {
                // Peek metadata to get timestamp
                std::byte* meta_buffer = queue->read(sizeof(LogMetadata));
                if (meta_buffer != nullptr) {
                    const auto* meta = reinterpret_cast<const LogMetadata*>(meta_buffer);
                    if (meta->timestamp < min_timestamp) {
                        min_timestamp = meta->timestamp;
                        selected_queue = queue;
                        selected_meta = meta;
                    }
                }
            }
        }
        
        // If found a log entry, process it
        if (selected_queue != nullptr && selected_meta != nullptr) {
            process_log_from_queue(selected_queue, selected_meta);
            return true;
        }
        
        return false;
    }

    /**
     * @brief Process a specific log entry from a queue
     * @param queue The queue to read from
     * @param meta The metadata (already peeked)
     */
    void process_log_from_queue(Queue* queue, const LogMetadata* metadata) {
        // Commit metadata read
        queue->commit_read(sizeof(LogMetadata));
        
        // Now read the arguments
        std::byte* args_buffer = nullptr;
        if (metadata->args_size > 0) {
            args_buffer = queue->read(metadata->args_size);
            if (args_buffer == nullptr) {
                // Failed to read args - this should not happen if data is correct
                return;
            }
        }
        
        // Get a writer for in-place string construction in output buffer
        auto writer = output_buffer_.get_writer();
        
        // Build the formatted string directly in the output buffer
        // Format: [LEVEL] HH:MM:SS:sss message
        writer.append(level_to_string(metadata->level));
        writer.append(" ");
        writer.append(format_timestamp(metadata->timestamp));
        writer.append(" ");

        // Decode arguments using the stored decoder, writes directly to writer
        if (metadata->decoder != nullptr && args_buffer != nullptr) {
            metadata->decoder(args_buffer, writer);
        }
        
        // Writer will finalize automatically on destruction
        
        // Commit args read
        if (metadata->args_size > 0) {
            queue->commit_read(metadata->args_size);
        }
    }

    /**
     * @brief Convert log level to string
     */
    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "[TRACE]";
            case LogLevel::DEBUG: return "[DEBUG]";
            case LogLevel::INFO:  return "[INFO]";
            case LogLevel::WARN:  return "[WARN]";
            case LogLevel::ERROR: return "[ERROR]";
            case LogLevel::FATAL: return "[FATAL]";
            default: return "[UNKNOWN]";
        }
    }

    /**
     * @brief Format timestamp to HH:MM:SS:sss
     * @param timestamp_ns Timestamp in nanoseconds
     * @return Formatted time string
     */
    static std::string format_timestamp(uint64_t timestamp_ns) {
        // Convert nanoseconds to milliseconds
        uint64_t total_ms = timestamp_ns / 1000000;
        
        // Extract milliseconds part (0-999)
        uint32_t milliseconds = total_ms - (total_ms / 1000) * 1000;
        
        // Convert to seconds
        uint64_t total_seconds = total_ms / 1000;
        
        // Fast modulo for seconds in day (86400 = 24*60*60)
        // Using the fact that we only care about time within a day
        total_seconds = total_seconds - (total_seconds / 86400) * 86400;
        
        // Extract hours (0-23)
        uint32_t hours = total_seconds / 3600;
        total_seconds -= hours * 3600;
        
        // Extract minutes (0-59)
        uint32_t minutes = total_seconds / 60;
        
        // Extract seconds (0-59)
        uint32_t seconds = total_seconds - minutes * 60;
        
        // Pre-computed digit table for faster conversion
        static constexpr char digits[200] = {
            '0','0','0','1','0','2','0','3','0','4','0','5','0','6','0','7','0','8','0','9',
            '1','0','1','1','1','2','1','3','1','4','1','5','1','6','1','7','1','8','1','9',
            '2','0','2','1','2','2','2','3','2','4','2','5','2','6','2','7','2','8','2','9',
            '3','0','3','1','3','2','3','3','3','4','3','5','3','6','3','7','3','8','3','9',
            '4','0','4','1','4','2','4','3','4','4','4','5','4','6','4','7','4','8','4','9',
            '5','0','5','1','5','2','5','3','5','4','5','5','5','6','5','7','5','8','5','9',
            '6','0','6','1','6','2','6','3','6','4','6','5','6','6','6','7','6','8','6','9',
            '7','0','7','1','7','2','7','3','7','4','7','5','7','6','7','7','7','8','7','9',
            '8','0','8','1','8','2','8','3','8','4','8','5','8','6','8','7','8','8','8','9',
            '9','0','9','1','9','2','9','3','9','4','9','5','9','6','9','7','9','8','9','9'
        };
        
        // Format as HH:MM:SS:sss using lookup table
        char buffer[13];  // "HH:MM:SS:sss\0"
        
        // Hours (HH)
        buffer[0] = digits[hours * 2];
        buffer[1] = digits[hours * 2 + 1];
        buffer[2] = ':';
        
        // Minutes (MM)
        buffer[3] = digits[minutes * 2];
        buffer[4] = digits[minutes * 2 + 1];
        buffer[5] = ':';
        
        // Seconds (SS)
        buffer[6] = digits[seconds * 2];
        buffer[7] = digits[seconds * 2 + 1];
        buffer[8] = ':';
        
        // Milliseconds (sss)
        uint32_t ms_hundreds = milliseconds / 100;
        uint32_t ms_remainder = milliseconds - ms_hundreds * 100;
        buffer[9] = '0' + ms_hundreds;
        buffer[10] = digits[ms_remainder * 2];
        buffer[11] = digits[ms_remainder * 2 + 1];
        buffer[12] = '\0';
        
        return std::string(buffer, 12);
    }

    std::vector<Queue*> queues_;           // All registered thread queues
    StringRingBuffer output_buffer_;       // Output buffer for formatted strings
    std::atomic<bool> running_;            // Backend running flag
    std::thread consumer_thread_;          // Backend consumer thread
};

} // namespace logZ
