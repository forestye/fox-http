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

            // Consume the header bytes BEFORE starting any async operation to
            // avoid multi-thread io_context race (see CHANGELOG 2026-02-18).
            request_buffer_.consume(bytes_transferred);

            HttpRequest request;
            if (!request.parse_header(header_data)) {
                HTTPSERVER_LOG("Connection(" << id() << ")::read() - malformed header");
                socket_.close();
                return;
            }

            // TODO(Phase 2): eager-read body according to Content-Length.
            // For Phase 1, we only support header-only requests (GET etc.).

            HttpResponse response;
            handler_.handle(request, response);

            // Decide keep-alive based on request's Connection header.
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
