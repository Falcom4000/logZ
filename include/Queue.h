#pragma once

#include "RingBytes.h"
#include <atomic>
#include <memory>
#include <cstddef>

namespace logZ {

/**
 * @brief A queue that manages multiple RingBytes in a linked list
 * 
 * When a RingBytes becomes full, a new one with double the size is automatically created.
 * The producer writes to the newest RingBytes, while the consumer reads from the oldest.
 * Old RingBytes are destroyed after the consumer finishes reading them.
 */
class Queue {
private:
    // Cache line aligned node to prevent false sharing
    struct alignas(64) Node {
        std::unique_ptr<RingBytes> ring;
        std::atomic<Node*> next;
        size_t capacity;
        
        explicit Node(size_t cap)
            : ring(std::make_unique<RingBytes>(cap))
            , next(nullptr)
            , capacity(cap) {
        }
    };

public:
    // Maximum capacity for a single node (64MB)
    static constexpr size_t MAX_NODE_CAPACITY = 64 * 1024 * 1024;
    
    /**
     * @brief Constructor
     * @param initial_capacity Initial capacity of the first RingBytes
     */
    explicit Queue(size_t initial_capacity = 4096)
        : initial_capacity_(initial_capacity)
        , write_node_(nullptr)
        , read_node_(nullptr) {
        // Create the first node
        Node* first_node = new Node(initial_capacity);
        write_node_.store(first_node, std::memory_order_relaxed);
        read_node_.store(first_node, std::memory_order_relaxed);
    }

    ~Queue() {
        // Clean up all nodes
        Node* current = read_node_.load(std::memory_order_relaxed);
        while (current != nullptr) {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

    // Disable copy and move
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    /**
     * @brief Reserve space for writing
     * @param size Number of bytes to reserve
     * @return Pointer to the reserved space, or nullptr if size is invalid
     * 
     * If the current RingBytes doesn't have enough space, a new one with double
     * the capacity will be created automatically.
     */
    std::byte* reserve_write(size_t size) {
        if (size == 0) {
            return nullptr;
        }

        Node* current_write = write_node_.load(std::memory_order_acquire);
        std::byte* ptr = current_write->ring->reserve_write(size);
        
        // Hot path: Usually succeeds on first try
        if (ptr != nullptr) [[likely]] {
            return ptr;
        }

        // Current RingBytes is full
        // If already at max capacity (64MB), reject the write (drop message)
        if (current_write->capacity >= MAX_NODE_CAPACITY) [[unlikely]] {
            return nullptr;  // Drop message when at max capacity
        }
        
        // Create a new node with double capacity, capped at MAX_NODE_CAPACITY
        size_t new_capacity = current_write->capacity * 2;
        if (new_capacity > MAX_NODE_CAPACITY) {
            new_capacity = MAX_NODE_CAPACITY;
        }
        
        // Make sure new capacity is at least large enough for this write
        while (new_capacity < size && new_capacity < MAX_NODE_CAPACITY) {
            new_capacity *= 2;
            if (new_capacity > MAX_NODE_CAPACITY) {
                new_capacity = MAX_NODE_CAPACITY;
            }
        }
        
        // If size is larger than MAX_NODE_CAPACITY, reject it
        if (size > MAX_NODE_CAPACITY) {
            return nullptr;
        }
        
        Node* new_node = new Node(new_capacity);
        
        // Try to reserve in the new node
        ptr = new_node->ring->reserve_write(size);
        if (ptr == nullptr) {
            // Size is too large even for the new node
            delete new_node;
            return nullptr;
        }
        
        // Link the new node
        current_write->next.store(new_node, std::memory_order_release);
        write_node_.store(new_node, std::memory_order_release);
        
        return ptr;
    }

    /**
     * @brief Write data to the queue
     * @param data Pointer to the data to write
     * @param size Number of bytes to write
     * @return Pointer to where the data was written, or nullptr on failure
     */
    std::byte* write(const void* data, size_t size) {
        if (size == 0 || data == nullptr) {
            return nullptr;
        }

        Node* current_write = write_node_.load(std::memory_order_acquire);
        std::byte* ptr = current_write->ring->write(data, size);
        
        // Hot path: Usually succeeds on first try
        if (ptr != nullptr) [[likely]] {
            return ptr;
        }

        // Current RingBytes is full
        // If already at max capacity (64MB), reject the write (drop message)
        if (current_write->capacity >= MAX_NODE_CAPACITY) [[unlikely]] {
            return nullptr;  // Drop message when at max capacity
        }
        
        // Create a new node with double capacity, capped at MAX_NODE_CAPACITY
        size_t new_capacity = current_write->capacity * 2;
        if (new_capacity > MAX_NODE_CAPACITY) {
            new_capacity = MAX_NODE_CAPACITY;
        }
        
        // Make sure new capacity is at least large enough for this write
        while (new_capacity < size && new_capacity < MAX_NODE_CAPACITY) {
            new_capacity *= 2;
            if (new_capacity > MAX_NODE_CAPACITY) {
                new_capacity = MAX_NODE_CAPACITY;
            }
        }
        
        // If size is larger than MAX_NODE_CAPACITY, reject it
        if (size > MAX_NODE_CAPACITY) {
            return nullptr;
        }
        
        Node* new_node = new Node(new_capacity);
        
        // Write to the new node
        ptr = new_node->ring->write(data, size);
        if (ptr == nullptr) {
            // Size is too large even for the new node
            delete new_node;
            return nullptr;
        }
        
        // Link the new node
        current_write->next.store(new_node, std::memory_order_release);
        write_node_.store(new_node, std::memory_order_release);
        
        return ptr;
    }

    /**
     * @brief Read data from the queue
     * @param size Number of bytes to read
     * @return Pointer to the data, or nullptr if not enough data available
     */
    std::byte* read(size_t size) {
        if (size == 0) {
            return nullptr;
        }

        Node* current_read = read_node_.load(std::memory_order_acquire);
        std::byte* ptr = current_read->ring->read(size);
        
        if (ptr != nullptr) {
            return ptr;
        }

        // Check if we need to switch to the next node
        Node* next_node = current_read->next.load(std::memory_order_acquire);
        if (next_node == nullptr) {
            // No more data available
            return nullptr;
        }

        // Current RingBytes is exhausted, check if it's completely empty
        if (current_read->ring->available_read() == 0) {
            // Switch to the next node
            read_node_.store(next_node, std::memory_order_release);
            
            // Delete the old node
            delete current_read;
            
            // Try to read from the new node
            return next_node->ring->read(size);
        }

        // Current node still has some data, but not enough for this read
        return nullptr;
    }

    /**
     * @brief Commit the read operation
     * @param size Number of bytes to commit as read
     */
    void commit_read(size_t size) {
        Node* current_read = read_node_.load(std::memory_order_acquire);
        current_read->ring->commit_read(size);
        
        // Check if we should clean up and switch to the next node
        if (current_read->ring->available_read() == 0) {
            Node* next_node = current_read->next.load(std::memory_order_acquire);
            if (next_node != nullptr) {
                // Switch to the next node
                read_node_.store(next_node, std::memory_order_release);
                
                // Delete the old node
                delete current_read;
            }
        }
    }

    /**
     * @brief Get the total number of bytes available for reading across all nodes
     * @return Number of bytes available
     */
    size_t available_read() const {
        size_t total = 0;
        Node* current = read_node_.load(std::memory_order_acquire);
        
        while (current != nullptr) {
            total += current->ring->available_read();
            Node* next = current->next.load(std::memory_order_acquire);
            if (next == nullptr || current == write_node_.load(std::memory_order_acquire)) {
                break;
            }
            current = next;
        }
        
        return total;
    }
    
    /**
     * @brief Check if the queue is empty (no data available to read)
     * @return true if empty, false otherwise
     */
    bool is_empty() const {
        return available_read() == 0;
    }

    /**
     * @brief Get the available space in the current write node
     * @return Number of bytes available for writing in current node
     */
    size_t available_write() const {
        Node* current_write = write_node_.load(std::memory_order_acquire);
        return current_write->ring->available_write();
    }

    /**
     * @brief Get the capacity of the current write node
     * @return Capacity in bytes
     */
    size_t current_capacity() const {
        Node* current_write = write_node_.load(std::memory_order_acquire);
        return current_write->capacity;
    }

    /**
     * @brief Get the number of RingBytes nodes in the queue
     * @return Number of nodes
     */
    size_t node_count() const {
        size_t count = 0;
        Node* current = read_node_.load(std::memory_order_acquire);
        Node* write = write_node_.load(std::memory_order_acquire);
        
        while (current != nullptr) {
            count++;
            if (current == write) {
                break;
            }
            current = current->next.load(std::memory_order_acquire);
        }
        
        return count;
    }

private:
    const size_t initial_capacity_;
    alignas(64) std::atomic<Node*> write_node_;  // Points to the current node for writing
    alignas(64) std::atomic<Node*> read_node_;   // Points to the current node for reading
};

}  // namespace logZ
