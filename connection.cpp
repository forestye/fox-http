/*
Implements Connection class methods.
- start(): Initiates client request reading.
- read(): Uses boost::asio::async_read_until() for asynchronous data reading until HTTP request header end marker is encountered. Then, parses request and calls handle_request() for request handling.
- write(): Uses boost::asio::async_write() for asynchronous response data writing to client. Post writing, bidirectional communication of connection is closed.
- handle_request(): Delegates request handling to RoutingModule, responsible for response generation based on request.
*/

#include "connection.h"
#include "server_status.h"
#include <iostream>
#include <ctime>
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
    return duration.count() > IDLE_TIMEOUT_SECONDS.count();
}

void Connection::read() {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n\r\n",
    [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        if (!ec) {
            last_active_time_ = std::chrono::system_clock::now(); // Update last active time
            HttpRequest request;
            HttpResponse response;

            std::istream request_stream(&request_buffer_);
            request.parse(request_stream);

            handle_request(request, response);

            std::string response_string = response.to_string();
            write(response_string);
        } else {
            if (ec == asio::error::eof) {
                socket_.close();
            } else if (ec == asio::error::operation_aborted) {
				cout << "Connection("<<get_id()<<")::read() - Operation cancelled." << endl;
				socket_.close();
			} else {
                cout << "Connection("<<get_id()<<")::read() - Error: " << ec.message() << endl;
            }
        }
    });
}


void Connection::write(const std::string& data) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
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
                cout << "Connection("<<get_id()<<")::write() - Error: " << ec.message() << endl;
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
