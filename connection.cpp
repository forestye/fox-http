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
#include "server_status.h"
//#include "timer_manager.h"
#include <iostream>
#include <ctime>
#include "logger.h"
using namespace std;

Connection::Connection(asio::io_context& io_context, RoutingModule& routing_module)
    : socket_(io_context), routing_module_(routing_module), keep_alive_(false) {
	ServerStatus::instance().increment_connection_count();
    last_active_time_ = std::chrono::system_clock::now();
}
Connection::~Connection() {
	ServerStatus::instance().decrement_connection_count();
    if (socket_.is_open()) {
        close();
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

void Connection::close() {
    if (socket_.is_open()) {
        socket_.close();
    }
}

bool Connection::is_idle() const {
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - last_active_time_);
	//cout<<"Connection("<<get_id()<<")::is_idle() - "<<duration.count()<<" seconds,"<<"IDLE_TIMEOUT_SECONDS.count()="<<IDLE_TIMEOUT_SECONDS.count()<<endl;
    return duration.count() > IDLE_TIMEOUT_SECONDS.count();
}

void Connection::read() {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n\r\n",
    [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        if (!ec) {
            is_processing_.store(true, std::memory_order_relaxed);
            last_active_time_ = std::chrono::system_clock::now(); // Update last active time
            HttpRequest request;
            HttpResponse response;

            std::istream request_stream(&request_buffer_);
            request.parse(request_stream);

            handle_request(request, response);

            std::string response_string = response.to_string();
            write(response_string);
            request_buffer_.consume(bytes_transferred);
        } else {
            is_processing_.store(false, std::memory_order_relaxed);
            if (ec == asio::error::eof) {
                socket_.close();
            } else if (ec == asio::error::operation_aborted) {
                DEBUG_LOG("Connection(" << get_id() << ")::read() - Operation cancelled.");
                socket_.close();
            } else {
                DEBUG_LOG("Connection(" << get_id() << ")::read() - Error: " << ec.message());
            }
        }
    });
}


void Connection::write(const std::string& data) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            is_processing_.store(false, std::memory_order_relaxed);
            if (!ec) {
                last_active_time_ = std::chrono::system_clock::now(); // Update last active time
                if (keep_alive_) {
                    read();
                } else {
                    if (socket_.is_open()) {
                        socket_.shutdown(tcp::socket::shutdown_both);
                    }
                }
            } else {
                DEBUG_LOG("Connection(" << get_id() << ")::write() - Error: " << ec.message());
            }
        });
}

void Connection::handle_request(const HttpRequest& request, HttpResponse& response) {
    routing_module_.route_request(request, response);
    auto connection_header = request.get_header("Connection");
    if (connection_header.empty() || connection_header == "close") {
        response.set_header("Connection", "close");
        keep_alive_ = false;
    } else {
        response.set_header("Connection", "keep-alive");
        keep_alive_ = true;
    }
}
