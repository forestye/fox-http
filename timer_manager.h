#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H

#include <boost/asio.hpp>
#include <unordered_map>
#include <chrono>
#include <memory>
#include "connection.h"
#include <mutex>

namespace asio = boost::asio;
using asio::ip::tcp;

class TimerManager {
public:
    // Get the singleton instance of the TimerManager
    static TimerManager& instance() {
        static TimerManager instance_;
        return instance_;
    }

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    // Initialize the TimerManager with an io_context and interval
    void init(asio::io_context& io_context, std::chrono::seconds interval) {
        timer_ = std::make_unique<asio::steady_timer>(io_context, interval);
        interval_ = interval;
    }

    // Add a connection to be managed
    void add_connection(std::shared_ptr<Connection> connection,size_t id);

    // Remove a connection from management
    void remove_connection(size_t id);

    // Start the timer
    void start_timer();

    // Stop the timer
    void stop_timer();

private:
    // Private constructor for the singleton instance
    TimerManager() = default;

    // Timer for checking idle connections
    std::unique_ptr<asio::steady_timer> timer_;

    // Interval at which to check for idle connections
    std::chrono::seconds interval_;

    // Managed connections
    std::unordered_map<std::size_t, std::weak_ptr<Connection>> connections_;

	std::mutex mutex_;

    // Check for and close idle connections
    void check_idle_connections();
};

#endif // TIMER_MANAGER_H
