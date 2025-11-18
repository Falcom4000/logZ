#include "Queue.h"
#include "RingBytes.h"
#include <gtest/gtest.h>
#include <cstring>
#include <thread>
#include <vector>
#include <string>
#include <atomic>

using namespace logZ;

// Test fixture for Queue tests
class QueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }

    // Helper function to compare bytes
    bool compare_bytes(const std::byte* data, const char* expected, size_t size) {
        return std::memcmp(data, expected, size) == 0;
    }
};

// Test 1: Basic write and read
TEST_F(QueueTest, BasicWriteRead) {
    Queue queue(64);
    
    const char* msg = "Hello, World!";
    size_t msg_len = std::strlen(msg);
    
    // Write
    std::byte* write_ptr = queue.reserve_write(msg_len);
    ASSERT_NE(write_ptr, nullptr);
    std::memcpy(write_ptr, msg, msg_len);
    
    // Read
    std::byte* read_ptr = queue.read(msg_len);
    ASSERT_NE(read_ptr, nullptr);
    EXPECT_TRUE(compare_bytes(read_ptr, msg, msg_len));
    queue.commit_read(msg_len);
}

// Test 2: Reserve and manual write
TEST_F(QueueTest, ReserveWrite) {
    Queue queue(64);
    
    const char* msg = "Reserved!";
    size_t msg_len = std::strlen(msg);
    
    // Reserve
    std::byte* write_ptr = queue.reserve_write(msg_len);
    ASSERT_NE(write_ptr, nullptr);
    
    // Manual write
    std::memcpy(write_ptr, msg, msg_len);
    
    // Read
    std::byte* read_ptr = queue.read(msg_len);
    ASSERT_NE(read_ptr, nullptr);
    EXPECT_TRUE(compare_bytes(read_ptr, msg, msg_len));
    queue.commit_read(msg_len);
}

// Test 3: Auto expansion when full
TEST_F(QueueTest, AutoExpansion) {
    Queue queue(32);  // Small initial capacity
    
    // Write data that fills the first buffer
    const char* msg1 = "FirstBuffer_____________";  // 24 bytes
    size_t msg1_len = std::strlen(msg1);
    
    std::byte* ptr1 = queue.reserve_write(msg1_len);
    ASSERT_NE(ptr1, nullptr);
    std::memcpy(ptr1, msg1, msg1_len);
    
    EXPECT_EQ(queue.node_count(), 1);
    
    // Write more data to trigger expansion
    const char* msg2 = "SecondBuffer____________";  // 24 bytes
    size_t msg2_len = std::strlen(msg2);
    
    std::byte* ptr2 = queue.reserve_write(msg2_len);
    ASSERT_NE(ptr2, nullptr);
    std::memcpy(ptr2, msg2, msg2_len);
    
    // Should have created a new node
    EXPECT_EQ(queue.node_count(), 2);
    
    // Verify new capacity is double
    EXPECT_EQ(queue.current_capacity(), 64);
    
    // Read first message
    std::byte* read1 = queue.read(msg1_len);
    ASSERT_NE(read1, nullptr);
    EXPECT_TRUE(compare_bytes(read1, msg1, msg1_len));
    queue.commit_read(msg1_len);
    
    // After committing, old node should be deleted
    EXPECT_EQ(queue.node_count(), 1);
    
    // Read second message
    std::byte* read2 = queue.read(msg2_len);
    ASSERT_NE(read2, nullptr);
    EXPECT_TRUE(compare_bytes(read2, msg2, msg2_len));
    queue.commit_read(msg2_len);
}

// Test 4: Multiple writes and reads
TEST_F(QueueTest, MultipleOperations) {
    Queue queue(128);
    
    std::vector<std::string> messages = {
        "Message1",
        "Message2",
        "Message3",
        "Message4",
        "Message5"
    };
    
    // Write all messages
    for (const auto& msg : messages) {
        std::byte* ptr = queue.reserve_write(msg.size());
        ASSERT_NE(ptr, nullptr);
        std::memcpy(ptr, msg.c_str(), msg.size());
    }
    
    // Read all messages
    for (const auto& msg : messages) {
        std::byte* ptr = queue.read(msg.size());
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(compare_bytes(ptr, msg.c_str(), msg.size()));
        queue.commit_read(msg.size());
    }
}

// Test 5: Available read/write
TEST_F(QueueTest, Available) {
    Queue queue(100);
    
    size_t initial_write = queue.available_write();
    EXPECT_EQ(initial_write, 100);
    EXPECT_EQ(queue.available_read(), 0);
    
    // Write 50 bytes
    const char data[50] = {};
    std::byte* ptr_write = queue.reserve_write(50);
    ASSERT_NE(ptr_write, nullptr);
    std::memcpy(ptr_write, data, 50);
    
    EXPECT_EQ(queue.available_read(), 50);
    EXPECT_EQ(queue.available_write(), 50);
    
    // Read 30 bytes
    std::byte* ptr = queue.read(30);
    ASSERT_NE(ptr, nullptr);
    queue.commit_read(30);
    
    EXPECT_EQ(queue.available_read(), 20);
    EXPECT_EQ(queue.available_write(), 80);
}

// Test 6: Edge cases
TEST_F(QueueTest, EdgeCases) {
    Queue queue(64);
    
    // Try to write 0 bytes
    std::byte* ptr = queue.reserve_write(0);
    EXPECT_EQ(ptr, nullptr);
    
    // Try to read 0 bytes
    ptr = queue.read(0);
    EXPECT_EQ(ptr, nullptr);
    
    // Try to read when empty
    ptr = queue.read(10);
    EXPECT_EQ(ptr, nullptr);
}

// Test 7: Large data expansion
TEST_F(QueueTest, LargeData) {
    Queue queue(64);
    
    // Write data larger than initial capacity
    std::vector<char> large_data(200, 'X');
    std::byte* ptr = queue.reserve_write(large_data.size());
    ASSERT_NE(ptr, nullptr);
    std::memcpy(ptr, large_data.data(), large_data.size());
    
    // Should have expanded to accommodate
    EXPECT_GE(queue.current_capacity(), 200);
    
    // Read back
    std::byte* read_ptr = queue.read(large_data.size());
    ASSERT_NE(read_ptr, nullptr);
    
    // Verify data
    for (size_t i = 0; i < large_data.size(); ++i) {
        EXPECT_EQ(static_cast<char>(read_ptr[i]), 'X');
    }
    
    queue.commit_read(large_data.size());
}

// Test 8: Chain of expansions
TEST_F(QueueTest, ChainExpansion) {
    Queue queue(32);
    
    // Fill first buffer
    std::vector<char> data1(28, 'A');
    std::byte* ptr1 = queue.reserve_write(data1.size());
    ASSERT_NE(ptr1, nullptr);
    std::memcpy(ptr1, data1.data(), data1.size());
    
    // Trigger second buffer (64 bytes)
    std::vector<char> data2(60, 'B');
    std::byte* ptr2 = queue.reserve_write(data2.size());
    ASSERT_NE(ptr2, nullptr);
    std::memcpy(ptr2, data2.data(), data2.size());
    
    EXPECT_EQ(queue.node_count(), 2);
    
    // Trigger third buffer (128 bytes)
    std::vector<char> data3(120, 'C');
    std::byte* ptr3 = queue.reserve_write(data3.size());
    ASSERT_NE(ptr3, nullptr);
    std::memcpy(ptr3, data3.data(), data3.size());
    
    EXPECT_EQ(queue.node_count(), 3);
    EXPECT_EQ(queue.current_capacity(), 128);
    
    // Read and verify all data
    std::byte* ptr1 = queue.read(data1.size());
    ASSERT_NE(ptr1, nullptr);
    for (size_t i = 0; i < data1.size(); ++i) {
        EXPECT_EQ(static_cast<char>(ptr1[i]), 'A');
    }
    queue.commit_read(data1.size());
    
    EXPECT_EQ(queue.node_count(), 2);
    
    std::byte* ptr2 = queue.read(data2.size());
    ASSERT_NE(ptr2, nullptr);
    for (size_t i = 0; i < data2.size(); ++i) {
        EXPECT_EQ(static_cast<char>(ptr2[i]), 'B');
    }
    queue.commit_read(data2.size());
    
    EXPECT_EQ(queue.node_count(), 1);
    
    std::byte* ptr3 = queue.read(data3.size());
    ASSERT_NE(ptr3, nullptr);
    for (size_t i = 0; i < data3.size(); ++i) {
        EXPECT_EQ(static_cast<char>(ptr3[i]), 'C');
    }
    queue.commit_read(data3.size());
}

// Test 9: Producer-Consumer pattern (single-threaded simulation)
TEST_F(QueueTest, ProducerConsumer) {
    Queue queue(64);
    
    // Simulate interleaved production and consumption
    for (int i = 0; i < 10; ++i) {
        std::string msg = "Msg" + std::to_string(i);
        
        // Produce
        std::byte* ptr = queue.reserve_write(msg.size());
        ASSERT_NE(ptr, nullptr);
        std::memcpy(ptr, msg.c_str(), msg.size());
        
        // Consume
        std::byte* read_ptr = queue.read(msg.size());
        ASSERT_NE(read_ptr, nullptr);
        EXPECT_TRUE(compare_bytes(read_ptr, msg.c_str(), msg.size()));
        queue.commit_read(msg.size());
    }
    
    // Queue should be empty
    EXPECT_EQ(queue.available_read(), 0);
}

// Test 10: Multi-threaded producer-consumer
TEST_F(QueueTest, Multithreaded) {
    Queue queue(512);
    const int num_messages = 1000;
    std::atomic<bool> producer_done(false);
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            // Use fixed-size messages to avoid wrap-around issues
            char msg[32];
            std::snprintf(msg, sizeof(msg), "Msg%05d", i);
            size_t msg_len = std::strlen(msg);
            
            bool written = false;
            while (!written) {
                std::byte* ptr = queue.reserve_write(msg_len);
                if (ptr != nullptr) {
                    std::memcpy(ptr, msg, msg_len);
                    written = true;
                } else {
                    std::this_thread::yield();
                }
            }
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int consumed = 0;
        while (consumed < num_messages) {
            char expected[32];
            std::snprintf(expected, sizeof(expected), "Msg%05d", consumed);
            size_t expected_len = std::strlen(expected);
            
            std::byte* ptr = queue.read(expected_len);
            if (ptr != nullptr) {
                // Copy data to a buffer for comparison (handle wrap-around)
                std::vector<char> buffer(expected_len);
                std::memcpy(buffer.data(), ptr, expected_len);
                
                EXPECT_TRUE(std::memcmp(buffer.data(), expected, expected_len) == 0)
                    << "Mismatch at message " << consumed;
                
                queue.commit_read(expected_len);
                consumed++;
            } else {
                if (producer_done.load(std::memory_order_acquire) && 
                    queue.available_read() == 0) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        EXPECT_EQ(consumed, num_messages);
    });
    
    producer.join();
    consumer.join();
}

// Test fixture for RingBytes tests
class RingBytesTest : public ::testing::Test {
protected:
    bool compare_bytes(const std::byte* data, const char* expected, size_t size) {
        return std::memcmp(data, expected, size) == 0;
    }
};

// RingBytes: Basic operations
TEST_F(RingBytesTest, BasicOperations) {
    RingBytes ring(64);
    
    const char* msg = "Test Message";
    size_t msg_len = std::strlen(msg);
    
    // Write
    std::byte* write_ptr = ring.reserve_write(msg_len);
    ASSERT_NE(write_ptr, nullptr);
    std::memcpy(write_ptr, msg, msg_len);
    
    // Read
    std::byte* read_ptr = ring.read(msg_len);
    ASSERT_NE(read_ptr, nullptr);
    EXPECT_TRUE(compare_bytes(read_ptr, msg, msg_len));
    ring.commit_read(msg_len);
}

// RingBytes: Reserve and commit
TEST_F(RingBytesTest, ReserveCommit) {
    RingBytes ring(100);
    
    // Reserve space
    std::byte* ptr = ring.reserve_write(10);
    ASSERT_NE(ptr, nullptr);
    
    // Write manually
    for (int i = 0; i < 10; ++i) {
        ptr[i] = static_cast<std::byte>(i);
    }
    
    // Read and verify
    std::byte* read_ptr = ring.read(10);
    ASSERT_NE(read_ptr, nullptr);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(read_ptr[i], static_cast<std::byte>(i));
    }
    ring.commit_read(10);
}

// RingBytes: Capacity limits
TEST_F(RingBytesTest, CapacityLimits) {
    RingBytes ring(50);
    
    EXPECT_EQ(ring.capacity(), 50);
    EXPECT_EQ(ring.available_write(), 50);
    EXPECT_EQ(ring.available_read(), 0);
    
    // Fill the buffer
    std::vector<char> data(50, 'X');
    std::byte* ptr = ring.reserve_write(50);
    ASSERT_NE(ptr, nullptr);
    std::memcpy(ptr, data.data(), 50);
    
    EXPECT_EQ(ring.available_write(), 0);
    EXPECT_EQ(ring.available_read(), 50);
    
    // Try to write more - should fail
    ptr = ring.reserve_write(5);
    EXPECT_EQ(ptr, nullptr);
}

// RingBytes: Wrap around
TEST_F(RingBytesTest, WrapAround) {
    RingBytes ring(32);
    
    // Write 20 bytes
    std::vector<char> data1(20, 'A');
    std::byte* ptr1 = ring.reserve_write(20);
    ASSERT_NE(ptr1, nullptr);
    std::memcpy(ptr1, data1.data(), 20);
    
    // Read 20 bytes
    ring.read(20);
    ring.commit_read(20);
    
    // Write 25 bytes (will wrap around)
    std::vector<char> data2(25, 'B');
    std::byte* ptr = ring.reserve_write(25);
    ASSERT_NE(ptr, nullptr);
    std::memcpy(ptr, data2.data(), 25);
    
    // Read and verify
    std::byte* read_ptr = ring.read(25);
    ASSERT_NE(read_ptr, nullptr);
    ring.commit_read(25);
}
