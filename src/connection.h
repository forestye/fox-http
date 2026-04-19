#pragma once

#include "response_stream.h"

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <string_view>

namespace httpserver {

class HttpHandler;
class HttpRequest;
class HttpResponse;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::io_context& io_context,
               HttpHandler& handler,
               StreamDispatcher& dispatcher);
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
    void read_body(std::shared_ptr<HttpRequest> request, std::size_t body_len);
    void dispatch(std::shared_ptr<HttpRequest> request);
    void post_stream_work(std::shared_ptr<HttpRequest> request,
                          std::shared_ptr<HttpResponse> response);
    void finish_request();
    void write_buffered(HttpResponse& response);
    void replace_with_500(HttpResponse& response, std::string_view msg);

    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf request_buffer_;
    HttpHandler& handler_;
    StreamDispatcher& dispatcher_;
    bool keep_alive_ = false;
    std::chrono::system_clock::time_point last_active_time_;
    std::atomic<bool> is_processing_{false};

    // Implements ResponseStream by writing synchronously to socket_.
    class SocketStream;
    std::unique_ptr<SocketStream> socket_stream_;

    static constexpr std::chrono::seconds kIdleTimeout{30};
};

}  // namespace httpserver
