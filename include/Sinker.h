#pragma once

#include <cstddef>
#include <fstream>
#include <string>

namespace logZ {

/**
 * @brief Sinker writes data from buffer to disk
 * Handles file I/O and rotation
 */
class Sinker {
public:
    explicit Sinker(const std::string& filename, size_t max_file_size = 4 * 1024 * 1024)  // 4MB default
        : filename_(filename), max_file_size_(max_file_size), current_file_size_(0) {
        open_file();
    }

    ~Sinker() {
        close_file();
    }

    // Disable copy and move
    Sinker(const Sinker&) = delete;
    Sinker& operator=(const Sinker&) = delete;
    Sinker(Sinker&&) = delete;
    Sinker& operator=(Sinker&&) = delete;

    /**
     * @brief Write data to disk
     * @param data Pointer to data buffer
     * @param length Number of bytes to write
     * @return true if successful, false otherwise
     */
    bool write(const std::byte* data, size_t length) {
        if (!file_.is_open()) {
            return false;
        }

        // Check if we need to rotate the file
        if (current_file_size_ + length > max_file_size_) {
            rotate_file();
        }

        file_.write(reinterpret_cast<const char*>(data), length);
        
        if (file_.fail()) {
            return false;
        }

        current_file_size_ += length;
        return true;
    }

    /**
     * @brief Flush buffered data to disk
     */
    void flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    /**
     * @brief Get current file size
     */
    size_t current_file_size() const {
        return current_file_size_;
    }

    /**
     * @brief Check if file is open
     */
    bool is_open() const {
        return file_.is_open();
    }

private:
    /**
     * @brief Open the log file
     */
    void open_file() {
        file_.open(filename_, std::ios::binary | std::ios::app);
        
        if (file_.is_open()) {
            // Get current file size
            file_.seekp(0, std::ios::end);
            current_file_size_ = file_.tellp();
        }
    }

    /**
     * @brief Close the current log file
     */
    void close_file() {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    /**
     * @brief Rotate log file when it reaches max size
     */
    void rotate_file() {
        close_file();

        // Generate new filename with timestamp or counter
        std::string new_filename = filename_ + "." + std::to_string(rotation_counter_++);
        
        // Rename current file
        std::rename(filename_.c_str(), new_filename.c_str());

        // Open new file
        current_file_size_ = 0;
        open_file();
    }

    std::string filename_;
    size_t max_file_size_;
    size_t current_file_size_;
    size_t rotation_counter_{0};
    std::ofstream file_;
};

} // namespace logZ
