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
        // Format: [LEVEL] HH:MM:SS:sss: message
        writer.append(level_to_string(metadata->level));
        writer.append(" ");
        writer.append(format_timestamp(metadata->timestamp));
        writer.append(": ");
        
        // Decode arguments using the stored decoder
        if (metadata->decoder != nullptr && args_buffer != nullptr) {
            writer.append(metadata->decoder(args_buffer));
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
        
        // Extract time components
        uint64_t total_seconds = total_ms / 1000;
        uint32_t milliseconds = total_ms % 1000;
        
        uint32_t hours = (total_seconds / 3600) % 24;
        uint32_t minutes = (total_seconds / 60) % 60;
        uint32_t seconds = total_seconds % 60;
        
        // Format as HH:MM:SS:sss
        char buffer[13];  // "HH:MM:SS:sss\0"
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u:%03u",
                     hours, minutes, seconds, milliseconds);
        
        return std::string(buffer);
    }

    std::vector<Queue*> queues_;           // All registered thread queues
    StringRingBuffer output_buffer_;       // Output buffer for formatted strings
    std::atomic<bool> running_;            // Backend running flag
    std::thread consumer_thread_;          // Backend consumer thread
};

} // namespace logZ
