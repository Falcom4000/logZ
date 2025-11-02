#pragma once

#include <cstddef>
#include <string>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace logZ {

/**
 * @brief Sinker writes data using POSIX write() WITHOUT O_DIRECT
 * 
 * Performance characteristics:
 * - Uses page cache (kernel buffering)
 * - No alignment requirements
 * - Simpler implementation
 * - Good for comparison with O_DIRECT version
 * 
 * Default log directory: ./logs
 * Filename format: YYYY-MM-DD_i.log (i starts from 1)
 */
class Sinker {
public:
    explicit Sinker(const std::string& log_dir = "./logs", size_t max_file_size = 100 * 1024 * 1024)
        : log_dir_(log_dir), max_file_size_(max_file_size), 
          current_file_size_(0), daily_counter_(1), fd_(-1) {
        
        // Create logs directory if not exists
        std::filesystem::create_directories(log_dir_);
        
        // Generate initial filename
        update_current_date();
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
     * @brief Write data to disk using POSIX write()
     * @param data Pointer to data buffer
     * @param length Number of bytes to write
     * @return true if successful, false otherwise
     * 
     * Simple direct write - no alignment needed, kernel handles buffering
     */
    bool write(const std::byte* data, size_t length) {
        if (fd_ < 0) {
            return false;
        }

        // Check if date has changed (new day)
        check_date_change();

        // Check if we need to rotate the file
        if (current_file_size_ + length > max_file_size_) {
            rotate_file();
        }

        // Direct write - no alignment needed
        ssize_t written = ::write(fd_, data, length);
        
        if (written < 0) {
            return false;
        }

        current_file_size_ += written;
        return written == static_cast<ssize_t>(length);
    }

    /**
     * @brief Flush buffered data to disk
     */
    void flush() {
        if (fd_ >= 0) {
            // Use fdatasync for better performance (doesn't sync metadata)
            ::fdatasync(fd_);
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
        return fd_ >= 0;
    }
    
    /**
     * @brief Get current log filename
     */
    std::string current_filename() const {
        return current_filename_;
    }

private:
    /**
     * @brief Get current date string in YYYY-MM-DD format
     */
    std::string get_date_string() const {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_r(&time_t_now, &tm_now);
        
        char buffer[11];  // "YYYY-MM-DD\0"
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
                tm_now.tm_year + 1900, 
                tm_now.tm_mon + 1, 
                tm_now.tm_mday);
        return std::string(buffer);
    }
    
    /**
     * @brief Update current date and reset counter if date changed
     */
    void update_current_date() {
        current_date_ = get_date_string();
        find_next_counter();
    }
    
    /**
     * @brief Find next available counter for current date
     */
    void find_next_counter() {
        daily_counter_ = 1;
        
        if (std::filesystem::exists(log_dir_)) {
            for (const auto& entry : std::filesystem::directory_iterator(log_dir_)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    
                    if (filename.find(current_date_) == 0) {
                        size_t underscore_pos = filename.find('_', current_date_.length());
                        size_t dot_pos = filename.find(".log", underscore_pos);
                        
                        if (underscore_pos != std::string::npos && dot_pos != std::string::npos) {
                            std::string counter_str = filename.substr(underscore_pos + 1, 
                                                                     dot_pos - underscore_pos - 1);
                            try {
                                size_t counter = std::stoull(counter_str);
                                if (counter >= daily_counter_) {
                                    daily_counter_ = counter + 1;
                                }
                            } catch (...) {
                                // Ignore invalid filenames
                            }
                        }
                    }
                }
            }
        }
    }
    
    /**
     * @brief Generate filename based on current date and counter
     */
    std::string generate_filename() const {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s/%s_%zu.log", 
                log_dir_.c_str(), current_date_.c_str(), daily_counter_);
        return std::string(buffer);
    }
    
    /**
     * @brief Check if date has changed and rotate if needed
     */
    void check_date_change() {
        std::string new_date = get_date_string();
        if (new_date != current_date_) {
            close_file();
            current_date_ = new_date;
            daily_counter_ = 1;
            current_file_size_ = 0;
            open_file();
        }
    }

    /**
     * @brief Open the log file using POSIX open() WITHOUT O_DIRECT
     */
    void open_file() {
        current_filename_ = generate_filename();
        
        // Standard POSIX open - uses page cache
        // O_WRONLY: Write only
        // O_CREAT: Create if doesn't exist
        // O_APPEND: Append mode
        // O_CLOEXEC: Close on exec
        fd_ = ::open(current_filename_.c_str(), 
                    O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);  // 0644 permissions
        
        if (fd_ >= 0) {
            // Get current file size
            struct stat st;
            if (fstat(fd_, &st) == 0) {
                current_file_size_ = st.st_size;
            }
        }
    }

    /**
     * @brief Close the current log file
     */
    void close_file() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /**
     * @brief Rotate log file when it reaches max size
     */
    void rotate_file() {
        close_file();
        daily_counter_++;
        current_file_size_ = 0;
        open_file();
    }

    std::string log_dir_;              // Log directory path
    std::string current_date_;         // Current date string (YYYY-MM-DD)
    std::string current_filename_;     // Current log filename
    size_t max_file_size_;             // Maximum file size before rotation
    size_t current_file_size_;         // Current file size
    size_t daily_counter_;             // Daily counter (starts from 1)
    int fd_;                           // File descriptor for POSIX write
};

} // namespace logZ
