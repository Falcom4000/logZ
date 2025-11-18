#pragma once

#include "Decoder.h"
#include "Queue.h"
#include "Sinker.h"
#include "StringRingBuffer.h"
#include "LogTypes.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <pthread.h>  // For pthread_setaffinity_np

namespace logZ {

/**
 * @brief Backend logger that consumes from all thread queues
 * 
 * Architecture:
 * - Backend owns all Queues via unique_ptr (Owner)
 * - Worker threads hold raw Queue* pointers (Borrower)
 * - Double-buffering with dirty flag for lock-free traversal
 * - Delayed reclamation: Queue marked as abandoned on thread exit,
 *   actually destroyed by Backend after it's drained
 * 
 * Global Singleton:
 * - Backend is a global singleton to ensure it outlives all worker threads
 * - Use Backend<MinLevel>::get_instance() to access
 */
template<LogLevel MinLevel = LogLevel::INFO>
class Backend {
private:
    /**
     * @brief Wrapper for Queue with lifecycle metadata
     * Backend uses this to manage Queue ownership and state
     * Cache line aligned to prevent false sharing
     */
    struct alignas(64) QueueWrapper {
        std::unique_ptr<Queue> queue;              // Backend owns the Queue
        std::atomic<bool> abandoned{false};        // Set to true when thread exits
        std::thread::id owner_thread_id;           // Thread ID for debugging
        uint64_t created_timestamp;                // Creation time
        uint64_t abandoned_timestamp{0};           // When thread exited
        
        explicit QueueWrapper(std::unique_ptr<Queue> q, std::thread::id tid)
            : queue(std::move(q))
            , owner_thread_id(tid)
            , created_timestamp(get_current_timestamp_ns()) {}
        
        // Disable copy and move (managed by shared_ptr)
        QueueWrapper(const QueueWrapper&) = delete;
        QueueWrapper& operator=(const QueueWrapper&) = delete;
        QueueWrapper(QueueWrapper&&) = delete;
        QueueWrapper& operator=(QueueWrapper&&) = delete;
        
    private:
        static uint64_t get_current_timestamp_ns() {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
    };

public:
    /**
     * @brief Get global singleton instance
     * Thread-safe initialization guaranteed by C++11
     */
    static Backend& get_instance() {
        static Backend instance;
        return instance;
    }

    Backend(const std::string& log_dir = "./logs", size_t buffer_size = 1024 * 1024) 
        : running_(false), 
          output_buffer_(buffer_size), 
          sinker_(log_dir),
          consumer_thread_() {
        // Initialize both lists to point to the same empty vector
        auto empty = std::make_shared<std::vector<std::shared_ptr<QueueWrapper>>>();
        m_snapshot_list = empty;
        m_current_list = empty;
    }

    ~Backend() {
        stop();
        // Reclaim all remaining queues
        reclaim_all_queues();
    }
    
    // Disable copy and move
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend&&) = delete;

    /**
     * @brief Allocate a new Queue for a worker thread
     * Called when a thread first calls LOG_xxx
     * 
     * @return Queue* Raw pointer for the thread to use (Borrower)
     *         Backend retains ownership via shared_ptr
     */
    Queue* allocate_queue_for_thread() {
        std::lock_guard<std::mutex> lock(m_writer_mutex);
        
        // 1. Create Queue (Backend owns it)
        auto queue = std::make_unique<Queue>(4096);  // Initial 4KB capacity
        Queue* raw_ptr = queue.get();
        
        // 2. Wrap in QueueWrapper with shared_ptr
        auto wrapper = std::make_shared<QueueWrapper>(
            std::move(queue),
            std::this_thread::get_id()
        );
        
        // 3. Add to current_list (Copy-on-Write)
        auto new_current = std::make_shared<std::vector<std::shared_ptr<QueueWrapper>>>(*m_current_list);
        new_current->push_back(wrapper);
        m_current_list = new_current;
        
        // 4. Signal backend to sync
        m_dirty.store(true, std::memory_order_release);
        
        // 5. Return raw pointer to thread (Borrower)
        return raw_ptr;
    }
    
    /**
     * @brief Mark a Queue as abandoned (thread exiting)
     * Called by thread_local destructor when thread exits
     * 
     * Key: Only sets flag, does NOT destroy Queue
     * Backend will destroy Queue after it's drained
     * 
     * @param queue Raw pointer from thread
     */
    void mark_queue_abandoned(Queue* queue) {
        if (!queue) return;
        
        std::lock_guard<std::mutex> lock(m_writer_mutex);
        
        // Find corresponding wrapper (linear search, but queue count is small)
        for (const auto& wrapper : *m_current_list) {
            if (wrapper->queue.get() == queue) {
                // Set abandoned flag (atomic, Backend can see without lock)
                bool expected = false;
                if (wrapper->abandoned.compare_exchange_strong(expected, true)) {
                    // First time marking as abandoned
                    wrapper->abandoned_timestamp = get_current_timestamp_ns();
                }
                
                // Don't remove from current_list here!
                // Let Backend discover it via abandoned flag
                
                // Signal backend to check for abandoned queues
                m_dirty.store(true, std::memory_order_release);
                return;
            }
        }
    }

    /**
     * @brief Start the backend consumer thread
     * @param cpu_id CPU core ID to bind to (optional, -1 means no binding)
     */
    void start(int cpu_id = -1) {
        if (running_.exchange(true)) {
            return; // Already running
        }

        consumer_thread_ = std::thread([this, cpu_id]() {
            // Set CPU affinity if cpu_id is specified
            if (cpu_id >= 0) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_id, &cpuset);
                
                pthread_t thread = pthread_self();
                int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
                
                if (result != 0) {
                    // Log error but continue execution
                    // In production, you might want to handle this differently
                }
            }
            
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
        
        // Flush remaining data to disk
        flush_to_disk();
    }
    
    /**
     * @brief Flush output buffer to disk
     */
    void flush_to_disk() {
        output_buffer_.flush_to_sinker(&sinker_);
        // Note: flush_to_sinker already calls sinker->flush()
        // No need to flush again here
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

    /**
     * @brief Get the number of dropped messages
     * @return Total count of messages that couldn't be queued
     */
    uint64_t get_dropped_count() const {
        return dropped_messages_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset the dropped messages counter
     */
    void reset_dropped_count() {
        dropped_messages_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Increment the dropped messages counter (called by Logger)
     */
    void increment_dropped_count() {
        dropped_messages_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    /**
     * @brief Synchronize snapshot list with current list
     * Called when m_dirty flag is set
     * This is the only time Backend thread acquires the lock
     */
    void sync_snapshot_list() {
        std::lock_guard<std::mutex> lock(m_writer_mutex);
        m_snapshot_list = m_current_list;  // shared_ptr assignment, ref count managed automatically
        m_dirty.store(false, std::memory_order_relaxed);
    }
    
    /**
     * @brief Main consume loop running in backend thread
     */
    void consume_loop() {
        static int counter = 0;
        
        while (running_.load(std::memory_order_relaxed)) {
            // Check dirty flag (only atomic load, no lock)
            // Hot path: Usually not dirty
            if (m_dirty.load(std::memory_order_acquire)) [[unlikely]] {
                sync_snapshot_list();  // Sync when updates detected
            }
            
            bool processed_any = process_one_log();

            // Periodically reclaim abandoned queues and flush
            // Reduced frequency: every 50000 iterations instead of 10000
            if (++counter >= 50000) {  // Every 50000 iterations
                counter = 0;
                flush_to_disk();
                reclaim_abandoned_queues();
            }

            // If no work was done, sleep briefly to avoid busy-waiting
            // Hot path: Usually processes something
            if (!processed_any) [[unlikely]] {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // Final sync and drain
        if (m_dirty.load(std::memory_order_acquire)) {
            sync_snapshot_list();
        }
        while (process_one_log()) {
            // Keep processing until all queues are empty
        }
        
        // Final reclaim
        reclaim_abandoned_queues();
        
        // Final flush
        flush_to_disk();
    }

    /**
     * @brief Process one log entry with minimum timestamp from all queues
     * @return true if a log was processed, false if all queues are empty
     * 
     * This function is completely lock-free - it only traverses m_snapshot_list
     */
    bool process_one_log() {
        // Poll all registered queues and find the log entry with minimum timestamp
        Queue* selected_queue = nullptr;
        uint64_t min_timestamp = UINT64_MAX;
        
        // Traverse all queue heads to find minimum timestamp
        // LOCK-FREE: Direct traversal of m_snapshot_list, no atomic operations
        for (const auto& wrapper : *m_snapshot_list) {
            if (wrapper && wrapper->queue) [[likely]] {
                // Check if abandoned and empty - mark for reclaim
                if (wrapper->abandoned.load(std::memory_order_acquire)) [[unlikely]] {
                    if (wrapper->queue->is_empty()) {
                        // Will be reclaimed in next reclaim cycle
                        continue;
                    }
                    // Not empty yet, continue processing
                }
                
                // Peek metadata to get timestamp (read without commit)
                std::byte* meta_buffer = wrapper->queue->read(sizeof(Metadata));
                if (meta_buffer != nullptr) {
                    const auto* meta = reinterpret_cast<const Metadata*>(meta_buffer);
                    if (meta->timestamp < min_timestamp) {
                        min_timestamp = meta->timestamp;
                        selected_queue = wrapper->queue.get();
                    }
                }
            }
        }
        
        // If found a log entry, process it
        if (selected_queue != nullptr) {
            // Re-read and process the selected queue
            std::byte* meta_buffer = selected_queue->read(sizeof(Metadata));
            if (meta_buffer != nullptr) {
                const auto* metadata_ptr = reinterpret_cast<const Metadata*>(meta_buffer);
                process_log_from_queue(selected_queue, metadata_ptr);
                return true;
            }
        }
        
        return false;
    }

    /**
     * @brief Process a specific log entry from a queue
     * @param queue The queue to read from
     * @param metadata The metadata pointer (from peek)
     */
    void process_log_from_queue(Queue* queue, const Metadata* metadata_ptr) {
        // Copy metadata to stack FIRST
        Metadata metadata = *metadata_ptr;
        
        // Commit metadata read to advance pointer
        queue->commit_read(sizeof(Metadata));
        
        // Read args buffer
        std::byte* args_buffer = nullptr;
        if (metadata.args_size > 0) {
            args_buffer = queue->read(metadata.args_size);
            if (args_buffer == nullptr) {
                // Failed to read args
                return;
            }
        }
        
        // Process the log entry
        auto writer = output_buffer_.get_writer(&sinker_);
        
        writer.append(level_to_string(metadata.level));
        writer.append(" ");
        writer.append(format_timestamp(metadata.timestamp));
        writer.append(" ");

        if (metadata.decoder != nullptr) {
            using ActualDecoderFunc = void (*)(const std::byte*, StringRingBuffer::StringWriter&);
            auto actual_decoder = reinterpret_cast<ActualDecoderFunc>(metadata.decoder);
            actual_decoder(args_buffer, writer);
        }
        
        writer.append("\n");
        
        // Commit args read
        if (metadata.args_size > 0) {
            queue->commit_read(metadata.args_size);
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
    
    /**
     * @brief Reclaim abandoned queues that are empty
     * Called periodically in consume_loop
     * 
     * TWO-PHASE DELETION PROTOCOL:
     * Phase 1 (this call): Remove from m_current_list, move to m_pending_deletion
     *   - Updates m_current_list with Copy-on-Write
     *   - Sets m_dirty flag
     *   - Moves QueueWrapper to m_pending_deletion (shared_ptr keeps it alive)
     *   - Backend thread will sync m_snapshot_list in next iteration
     * 
     * Phase 2 (next call): Delete queues from m_pending_deletion
     *   - By this time, m_snapshot_list has been synced (no longer references deleted queues)
     *   - Safe to actually destroy the Queue objects
     * 
     * This ensures process_one_log() never accesses deleted memory
     */
    void reclaim_abandoned_queues() {
        std::lock_guard<std::mutex> lock(m_writer_mutex);
        
        // Phase 2: Delete previously marked queues
        // At this point, m_snapshot_list has been synced and no longer references them
        if (!m_pending_deletion.empty()) {
            m_pending_deletion.clear();  // shared_ptr ref count drops -> Queues deleted
        }
        
        // Phase 1: Find abandoned and empty queues
        std::vector<std::shared_ptr<QueueWrapper>> to_reclaim;
        for (const auto& wrapper : *m_current_list) {
            if (wrapper->abandoned.load(std::memory_order_acquire) &&
                wrapper->queue->is_empty()) {
                to_reclaim.push_back(wrapper);
            }
        }
        
        if (to_reclaim.empty()) {
            return;
        }
        
        // Remove from current_list (Copy-on-Write)
        auto new_current = std::make_shared<std::vector<std::shared_ptr<QueueWrapper>>>();
        for (const auto& wrapper : *m_current_list) {
            // Check if this wrapper is NOT in the to_reclaim list
            bool should_keep = true;
            for (const auto& reclaim_wrapper : to_reclaim) {
                if (wrapper.get() == reclaim_wrapper.get()) {
                    should_keep = false;
                    break;
                }
            }
            if (should_keep) {
                new_current->push_back(wrapper);
            }
        }
        m_current_list = new_current;
        m_dirty.store(true, std::memory_order_release);
        
        // Move to pending deletion (shared_ptr keeps them alive)
        m_pending_deletion = std::move(to_reclaim);
        
        // Queue will be deleted in NEXT call after m_snapshot_list syncs
    }
    
    /**
     * @brief Reclaim all queues (called in destructor)
     */
    void reclaim_all_queues() {
        std::lock_guard<std::mutex> lock(m_writer_mutex);
        
        // Clear lists (shared_ptr ref count drops -> all Queues deleted)
        m_current_list = std::make_shared<std::vector<std::shared_ptr<QueueWrapper>>>();
        m_snapshot_list = std::make_shared<std::vector<std::shared_ptr<QueueWrapper>>>();
        m_pending_deletion.clear();
    }
    
    /**
     * @brief Get current timestamp in nanoseconds
     */
    static uint64_t get_current_timestamp_ns() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }

    // ════════════════════════════════════════════════════════
    // Member Variables
    // ════════════════════════════════════════════════════════
    
    // Output and runtime (initialized first in constructor)
    std::atomic<bool> running_;            // Backend running flag
    StringRingBuffer output_buffer_;       // Output buffer for formatted strings
    Sinker sinker_;                        // File sinker for writing to disk
    std::thread consumer_thread_;          // Backend consumer thread
    
    // Statistics
    std::atomic<uint64_t> dropped_messages_{0};  // Counter for dropped messages
    
    // Queue ownership and management (simplified design)
    std::vector<std::shared_ptr<QueueWrapper>> m_pending_deletion; // Queues marked for deletion (two-phase)
    
    // Double-buffering for lock-free traversal
    // Now using shared_ptr<QueueWrapper> for automatic lifetime management
    std::shared_ptr<std::vector<std::shared_ptr<QueueWrapper>>> m_snapshot_list;  // Backend's snapshot for reading (lock-free)
    std::shared_ptr<std::vector<std::shared_ptr<QueueWrapper>>> m_current_list;   // Current list for updates (protected by mutex)
    std::mutex m_writer_mutex;                                                     // Protects current_list
    std::atomic<bool> m_dirty{false};                                              // Dirty flag for sync
};

} // namespace logZ
