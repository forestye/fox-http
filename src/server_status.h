#pragma once

#include <mutex>
#include <string>

namespace fox::http {

// Internal server statistics: connection counts, timeouts, peaks.
// Singleton accessed via instance(). Thread-safe.
class ServerStatus {
public:
    static ServerStatus& instance() {
        static ServerStatus s;
        return s;
    }

    int connection_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_count_;
    }
    void increment_connection_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++connection_count_;
        if (connection_count_ > max_connection_count_) max_connection_count_ = connection_count_;
    }
    void decrement_connection_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        --connection_count_;
    }

    int connection_object_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_object_count_;
    }
    void set_connection_object_count(int n) {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_object_count_ = n;
        if (n > max_connection_object_count_) max_connection_object_count_ = n;
    }

    int connection_timeout_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_timeout_count_;
    }
    void increment_connection_timeout_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++connection_timeout_count_;
    }

    std::string status_str() const;

private:
    ServerStatus() = default;

    mutable std::mutex mutex_;
    int connection_count_ = 0;
    int max_connection_count_ = 0;
    int connection_object_count_ = 0;
    int max_connection_object_count_ = 0;
    int connection_timeout_count_ = 0;
};

}  // namespace fox::http
