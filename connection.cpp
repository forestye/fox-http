/*
在这个文件中，我们实现了Connection类的方法。
start()方法开始读取客户端请求，
read()方法使用boost::asio::async_read_until()异步读取数据，直到遇到HTTP请求头的结束标记。
接着，我们解析请求并调用handle_request()方法处理请求。

write()方法使用boost::asio::async_write()异步写入响应数据到客户端。
写入完成后，我们关闭连接的双向通信。

handle_request()方法将处理请求的任务委托给RoutingModule，它负责根据请求生成响应。
*/
#include "connection.h"
#include <iostream>
#include <iostream>
#include <cstdlib>
#include <ctime>
using namespace std;

Connection::Connection(asio::io_context& io_context, RoutingModule& routing_module)
    : socket_(io_context), routing_module_(routing_module) {
}

Connection::~Connection() {
    if (socket_.is_open()) {
        socket_.close();
    }
}
tcp::socket& Connection::socket() {
    return socket_;
}

void Connection::start() {
    read();
}

void Connection::cancel() {
    if (socket_.is_open()) {
        socket_.cancel();
    }
}

void Connection::read() {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n\r\n",
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                if (ec == asio::error::eof) {
                    // Client closed the connection, just close the server-side connection
                    socket_.close();
                } else {
                    cout << "Connection::read() - Error: " << ec.message() << endl;
                }
            }
            else {
                HttpRequest request;
                HttpResponse response;

                std::istream request_stream(&request_buffer_);
                request.parse(request_stream);

                handle_request(request, response);

                std::string response_string = response.to_string();
                write(response_string);
            }
        });

    // Set a timeout
    //这里，我们为每个请求设置了一个5秒的超时时间。如果在5秒内服务器没有完成请求处理，连接将被关闭。
    /*
    boost::asio::steady_timer timer(socket_.get_executor());
    timer.expires_after(std::chrono::seconds(5));  // Set the timeout to 5 seconds
    timer.async_wait([self](const boost::system::error_code& ec) {
        if (!ec) {
            self->socket_.cancel();
        }
    });
    */
}

void Connection::write(const std::string& data) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec) {
                socket_.shutdown(tcp::socket::shutdown_both);
            } else {
                cout << "Connection::write() - Error: " << ec.message() << endl;
            }
        });
}

void Connection::handle_request(const HttpRequest& request, HttpResponse& response) {
    routing_module_.route_request(request, response);
}
