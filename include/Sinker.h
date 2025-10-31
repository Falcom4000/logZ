#pragma once

#include <cstddef>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstdio>

namespace logZ {

/**
 * @brief Sinker writes data from buffer to disk
 * Handles file I/O and rotation with date-based naming
 * Default log directory: ./logs
 * Filename format: YYYY-MM-DD_i.log (i starts from 1)
 */
class Sinker {
public:
    explicit Sinker(const std::string& log_dir = "./logs", size_t max_file_size = 100 * 1024 * 1024)  // 100MB default
        : log_dir_(log_dir), max_file_size_(max_file_size), current_file_size_(0), daily_counter_(1) {
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
     * @brief Write data to disk
     * @param data Pointer to data buffer
     * @param length Number of bytes to write
     * @return true if successful, false otherwise
     */
    bool write(const std::byte* data, size_t length) {
        if (!file_.is_open()) {
            return false;
        }

        // Check if date has changed (new day)
        check_date_change();

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
        
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << (tm_now.tm_year + 1900) << "-"
            << std::setw(2) << (tm_now.tm_mon + 1) << "-"
            << std::setw(2) << tm_now.tm_mday;
        return oss.str();
    }
    
    /**
     * @brief Update current date and reset counter if date changed
     */
    void update_current_date() {
        current_date_ = get_date_string();
        
        // Find the next available counter for today
        find_next_counter();
    }
    
    /**
     * @brief Find next available counter for current date
     */
    void find_next_counter() {
        daily_counter_ = 1;
        
        // Check existing log files to find the highest counter
        if (std::filesystem::exists(log_dir_)) {
            for (const auto& entry : std::filesystem::directory_iterator(log_dir_)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    
                    // Check if filename matches pattern: YYYY-MM-DD_i.log
                    if (filename.find(current_date_) == 0) {
                        // Extract counter
                        size_t underscore_pos = filename.find('_', current_date_.length());
                        size_t dot_pos = filename.find(".log", underscore_pos);
                        
                        if (underscore_pos != std::string::npos && dot_pos != std::string::npos) {
                            std::string counter_str = filename.substr(underscore_pos + 1, dot_pos - underscore_pos - 1);
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
        std::ostringstream oss;
        oss << log_dir_ << "/" << current_date_ << "_" << daily_counter_ << ".log";
        return oss.str();
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
     * @brief Open the log file
     */
    void open_file() {
        current_filename_ = generate_filename();
        file_.open(current_filename_, std::ios::binary | std::ios::app);
        
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

        // Increment counter for same day
        daily_counter_++;
        
        // Open new file with incremented counter
        current_file_size_ = 0;
        open_file();
    }

    std::string log_dir_;              // Log directory path
    std::string current_date_;         // Current date string (YYYY-MM-DD)
    std::string current_filename_;     // Current log filename
    size_t max_file_size_;             // Maximum file size before rotation
    size_t current_file_size_;         // Current file size
    size_t daily_counter_;             // Daily counter (starts from 1)
    std::ofstream file_;               // Output file stream
};

} // namespace logZ
