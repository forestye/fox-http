/*
在这个头文件中，我们定义了一个名为Connection的类，它表示与客户端之间的一个连接。
Connection类负责管理连接的生命周期、读取请求、写入响应以及调用RoutingModule来处理请求。
我们使用boost::asio库进行网络通信，并使用std::shared_ptr管理Connection对象的生命周期。
*/
#ifndef CONNECTION_H
#define CONNECTION_H

#include <boost/asio.hpp>
#include <memory>
#include <chrono>
#include <atomic>
#include "http_request.h"
#include "http_response.h"
#include "routing_module.h"

namespace asio = boost::asio;
using asio::ip::tcp;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    explicit Connection(asio::io_context& io_context, RoutingModule& routing_module);

    ~Connection();

    // Get the socket associated with the connection
    tcp::socket& socket();

    // Start processing the connection
    void start();

    void cancel();

	void close();

    // Check if the connection is idle
    bool is_idle() const;

    bool is_processing() const { return is_processing_.load(std::memory_order_relaxed); }

    // Get the unique ID of the connection
    std::size_t get_id() const { return reinterpret_cast<std::size_t>(this); }

private:
    // Read data from the socket
    void read();

    // Write data to the socket
    void write(std::shared_ptr<std::string> data);

    // Handle the request and generate a response
    void handle_request(const HttpRequest& request, HttpResponse& response);

    // Socket for the connection
    tcp::socket socket_;

    // Buffer for incoming data
    asio::streambuf request_buffer_;

    // Routing module for handling requests
    RoutingModule& routing_module_;

    int random_number_;

	bool keep_alive_;

    // The time when the connection was last active
    std::chrono::system_clock::time_point last_active_time_;

    static constexpr std::chrono::seconds IDLE_TIMEOUT_SECONDS = std::chrono::seconds(30);

    std::atomic<bool> is_processing_{false};
};

#endif // CONNECTION_H
