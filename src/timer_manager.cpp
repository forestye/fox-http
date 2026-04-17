#include "timer_manager.h"

#include "connection.h"
#include "logger.h"
#include "server_status.h"

namespace httpserver {

void TimerManager::init(boost::asio::io_context& io_context, std::chrono::seconds interval) {
    interval_ = interval;
    timer_ = std::make_unique<boost::asio::steady_timer>(io_context, interval);
}

void TimerManager::add_connection(std::shared_ptr<Connection> connection, std::size_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[id] = connection;
    ServerStatus::instance().set_connection_object_count(static_cast<int>(connections_.size()));
}

void TimerManager::remove_connection(std::size_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(id);
    ServerStatus::instance().set_connection_object_count(static_cast<int>(connections_.size()));
}

void TimerManager::start() {
    if (!timer_) return;
    timer_->expires_after(interval_);
    timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec) check_idle_connections();
    });
}

void TimerManager::stop() {
    if (timer_) timer_->cancel();
}

void TimerManager::check_idle_connections() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = connections_.begin(); it != connections_.end(); ) {
            if (auto conn = it->second.lock()) {
                if (!conn->is_processing() && conn->is_idle()) {
                    conn->cancel();
                    HTTPSERVER_LOG("connection timeout: " << conn->id());
                    ServerStatus::instance().increment_connection_timeout_count();
                }
                ++it;
            } else {
                it = connections_.erase(it);
            }
        }
        ServerStatus::instance().set_connection_object_count(static_cast<int>(connections_.size()));
    }
    start();
}

}  // namespace httpserver
