#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace fox::http {

class Connection;

class TimerManager {
public:
    static TimerManager& instance() {
        static TimerManager s;
        return s;
    }

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    void init(boost::asio::io_context& io_context, std::chrono::seconds interval);

    void add_connection(std::shared_ptr<Connection> connection, std::size_t id);
    void remove_connection(std::size_t id);

    void start();
    void stop();

private:
    TimerManager() = default;

    void check_idle_connections();

    std::unique_ptr<boost::asio::steady_timer> timer_;
    std::chrono::seconds interval_{3};
    std::unordered_map<std::size_t, std::weak_ptr<Connection>> connections_;
    std::mutex mutex_;
};

}  // namespace fox::http
