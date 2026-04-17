#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>

namespace httpserver {

class HttpHandler;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::io_context& io_context, HttpHandler& handler);
    ~Connection();

    boost::asio::ip::tcp::socket& socket() { return socket_; }

    void start();
    void cancel();
    void close();

    bool is_idle() const;
    bool is_processing() const { return is_processing_.load(std::memory_order_relaxed); }

    std::size_t id() const { return reinterpret_cast<std::size_t>(this); }

private:
    void read();
    void write(std::shared_ptr<std::string> data);

    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf request_buffer_;
    HttpHandler& handler_;
    bool keep_alive_ = false;
    std::chrono::system_clock::time_point last_active_time_;
    std::atomic<bool> is_processing_{false};

    static constexpr std::chrono::seconds kIdleTimeout{30};
};

}  // namespace httpserver
