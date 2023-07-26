#ifndef SERVER_STATUS_H
#define SERVER_STATUS_H

#include <string>
#include <mutex> // include mutex header

class ServerStatus {
public:
    static ServerStatus& instance() {
        static ServerStatus instance;
        return instance;
    }

    int get_connection_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_count_; 
    }
    void increment_connection_count() { 
        std::lock_guard<std::mutex> lock(mutex_);
        ++connection_count_; 
        ++increment_count_;
        check_and_update_max_connection_count();
    }
    void decrement_connection_count() { 
        std::lock_guard<std::mutex> lock(mutex_);
        --connection_count_;
        ++decrement_count_;
    }

    int get_increment_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return increment_count_; 
    }
    int get_decrement_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return decrement_count_; 
    }

    int get_connection_object_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_object_count_; 
    }
    void set_connection_object_count(int new_connection_object_count) { 
        std::lock_guard<std::mutex> lock(mutex_);
        connection_object_count_=new_connection_object_count;
        check_and_update_max_connection_object_count();
    }

    int get_max_connection_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return max_connection_count_; 
    }
    int get_max_connection_object_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return max_connection_object_count_; 
    }

	int get_connection_timeout_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_timeout_count_; 
    }
    void increment_connection_timeout_count() { 
        std::lock_guard<std::mutex> lock(mutex_);
        ++connection_timeout_count_; 
    }


    std::string get_status_str() const;

private:
    ServerStatus() { }

    void check_and_update_max_connection_count() {
        if (connection_count_ > max_connection_count_) {
            max_connection_count_ = connection_count_;
        }
    }

    void check_and_update_max_connection_object_count() {
        if (connection_object_count_ > max_connection_object_count_) {
            max_connection_object_count_ = connection_object_count_;
        }
    }

    int connection_count_ = 0;
    int max_connection_count_ = 0;
    int connection_object_count_ = 0;
    int max_connection_object_count_ = 0;
    
    int increment_count_ = 0;  // Added counter for increment_connection_count() calls
    int decrement_count_ = 0;  // Added counter for decrement_connection_count() calls

	int connection_timeout_count_ = 0;

    mutable std::mutex mutex_; // Added mutex for thread safety
};

#endif // SERVER_STATUS_H
