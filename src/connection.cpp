#include "connection.h"

#include "httpserver/http_handler.h"
#include "httpserver/http_request.h"
#include "httpserver/http_response.h"
#include "logger.h"
#include "server_status.h"

#include <string>
#include <string_view>

namespace httpserver {

namespace asio = boost::asio;
using asio::ip::tcp;

Connection::Connection(asio::io_context& io_context, HttpHandler& handler)
    : socket_(io_context), handler_(handler) {
    ServerStatus::instance().increment_connection_count();
    last_active_time_ = std::chrono::system_clock::now();
}

Connection::~Connection() {
    ServerStatus::instance().decrement_connection_count();
    if (socket_.is_open()) {
        close();
    }
}

void Connection::start() {
    read();
}

void Connection::cancel() {
    if (socket_.is_open()) socket_.cancel();
}

void Connection::close() {
    if (socket_.is_open()) socket_.close();
}

bool Connection::is_idle() const {
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - last_active_time_);
    return duration.count() > kIdleTimeout.count();
}

void Connection::dispatch(HttpRequest& request) {
    HttpResponse response;
    handler_.handle(request, response);

    auto conn_hdr = request.header("Connection");
    if (conn_hdr.empty() || conn_hdr == "close") {
        response.headers().insert("Connection", "close");
        keep_alive_ = false;
    } else {
        response.headers().insert("Connection", "keep-alive");
        keep_alive_ = true;
    }

    auto response_string = std::make_shared<std::string>(response.serialize());
    write(response_string);
}

void Connection::read() {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n\r\n",
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
                    socket_.close();
                } else {
                    HTTPSERVER_LOG("Connection(" << id() << ")::read() - Error: " << ec.message());
                }
                return;
            }

            is_processing_.store(true, std::memory_order_relaxed);
            last_active_time_ = std::chrono::system_clock::now();

            // bytes_transferred includes the delimiter "\r\n\r\n".
            auto begin = asio::buffers_begin(request_buffer_.data());
            std::string header_data(begin, begin + bytes_transferred);
            request_buffer_.consume(bytes_transferred);

            auto request = std::make_shared<HttpRequest>();
            if (!request->parse_header(header_data)) {
                HTTPSERVER_LOG("Connection(" << id() << ")::read() - malformed header");
                socket_.close();
                return;
            }

            std::size_t body_len = request->content_length();
            if (body_len == 0) {
                dispatch(*request);
                return;
            }

            read_body(request, body_len);
        });
}

void Connection::read_body(std::shared_ptr<HttpRequest> request, std::size_t body_len) {
    // Part of the body may already be sitting in request_buffer_ (async_read_until
    // can overshoot the delimiter). Drain what we have first.
    std::string body;
    body.reserve(body_len);
    std::size_t have = request_buffer_.size();
    if (have > 0) {
        std::size_t take = std::min(have, body_len);
        auto begin = asio::buffers_begin(request_buffer_.data());
        body.assign(begin, begin + take);
        request_buffer_.consume(take);
    }

    if (body.size() == body_len) {
        request->set_body(std::move(body));
        dispatch(*request);
        return;
    }

    std::size_t remaining = body_len - body.size();
    auto body_ptr = std::make_shared<std::string>(std::move(body));
    body_ptr->resize(body_len);

    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(body_ptr->data() + (body_len - remaining), remaining),
        asio::transfer_exactly(remaining),
        [this, self, request, body_ptr](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                HTTPSERVER_LOG("Connection(" << id() << ")::read_body() - Error: " << ec.message());
                socket_.close();
                return;
            }
            request->set_body(std::move(*body_ptr));
            dispatch(*request);
        });
}

void Connection::write(std::shared_ptr<std::string> data) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(*data),
        [this, self, data](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
            is_processing_.store(false, std::memory_order_relaxed);
            if (ec) {
                HTTPSERVER_LOG("Connection(" << id() << ")::write() - Error: " << ec.message());
                return;
            }
            last_active_time_ = std::chrono::system_clock::now();
            if (keep_alive_) {
                read();
            } else if (socket_.is_open()) {
                socket_.shutdown(tcp::socket::shutdown_both);
            }
        });
}

}  // namespace httpserver
