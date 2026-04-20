#include "connection.h"

#include "fox-http/http_handler.h"
#include "fox-http/http_request.h"
#include "fox-http/http_response.h"
#include "logger.h"
#include "server_status.h"

#include <algorithm>
#include <string>
#include <vector>

namespace fox::http {

namespace asio = boost::asio;
using asio::ip::tcp;

// Synchronous writer over a tcp::socket. Used by HttpResponse in Immediate
// and Stream modes. The caller is responsible for not interleaving writes
// from multiple threads; in our design only one handler / stream-lambda
// writes to a given socket at any time.
class Connection::SocketStream : public ResponseStream {
public:
    explicit SocketStream(tcp::socket& sock) : sock_(sock) {}

    bool write(const void* data, std::size_t n) override {
        boost::system::error_code ec;
        asio::write(sock_, asio::buffer(data, n), ec);
        return !ec;
    }

    bool writev(const struct iovec* iov, int iovcnt) override {
        std::vector<asio::const_buffer> bufs;
        bufs.reserve(static_cast<std::size_t>(iovcnt));
        for (int i = 0; i < iovcnt; ++i) {
            bufs.emplace_back(iov[i].iov_base, iov[i].iov_len);
        }
        boost::system::error_code ec;
        asio::write(sock_, bufs, ec);
        return !ec;
    }

private:
    tcp::socket& sock_;
};

Connection::Connection(asio::io_context& io_context,
                       HttpHandler& handler,
                       StreamDispatcher& dispatcher)
    : socket_(io_context),
      handler_(handler),
      dispatcher_(dispatcher),
      socket_stream_(std::make_unique<SocketStream>(socket_)) {
    ServerStatus::instance().increment_connection_count();
    last_active_time_ = std::chrono::system_clock::now();
}

Connection::~Connection() {
    ServerStatus::instance().decrement_connection_count();
    if (socket_.is_open()) close();
}

void Connection::start() {
    // TCP_NODELAY: disable Nagle's algorithm. HTTP responses are typically
    // assembled and sent in full; we never benefit from Nagle batching, and
    // we actively lose throughput when sync writes in Immediate mode trigger
    // 40 ms delayed-ACK stalls.
    boost::system::error_code ec;
    socket_.set_option(tcp::no_delay(true), ec);
    if (ec) {
        FOX_HTTP_LOG("Connection(" << id() << ") set TCP_NODELAY failed: "
                       << ec.message());
    }
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

// ── read / body ─────────────────────────────────────────────────────

void Connection::read() {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n\r\n",
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
                    socket_.close();
                } else {
                    FOX_HTTP_LOG("Connection(" << id() << ")::read() - Error: " << ec.message());
                }
                return;
            }

            is_processing_.store(true, std::memory_order_relaxed);
            last_active_time_ = std::chrono::system_clock::now();

            auto begin = asio::buffers_begin(request_buffer_.data());
            std::string header_data(begin, begin + bytes_transferred);
            request_buffer_.consume(bytes_transferred);

            auto request = std::make_shared<HttpRequest>();
            if (!request->parse_header(header_data)) {
                FOX_HTTP_LOG("Connection(" << id() << ")::read() - malformed header");
                socket_.close();
                return;
            }

            read_body(std::move(request));
        });
}

// Dispatch on Transfer-Encoding / Content-Length. Per RFC 7230, if both are
// present, Transfer-Encoding wins (and some stacks even reject). We prefer
// Transfer-Encoding: chunked when present.
void Connection::read_body(std::shared_ptr<HttpRequest> request) {
    auto te = request->header("Transfer-Encoding");
    // Case-insensitive substring check for "chunked". RFC 7230 allows comma-
    // separated codings; "chunked" must be the last one, but decoding works
    // correctly regardless of position.
    std::string te_lower(te);
    for (auto& c : te_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool chunked = te_lower.find("chunked") != std::string::npos;
    if (chunked) {
        auto acc = std::make_shared<std::string>();
        read_chunked_body(std::move(request), std::move(acc));
        return;
    }

    std::size_t body_len = request->content_length();
    if (body_len == 0) {
        dispatch(std::move(request));
        return;
    }
    read_fixed_body(std::move(request), body_len);
}

void Connection::read_fixed_body(std::shared_ptr<HttpRequest> request, std::size_t body_len) {
    // Drain any body bytes already in request_buffer_ from the async_read_until
    // overshoot.
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
        dispatch(std::move(request));
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
                FOX_HTTP_LOG("Connection(" << id() << ")::read_body() - Error: " << ec.message());
                socket_.close();
                return;
            }
            request->set_body(std::move(*body_ptr));
            dispatch(request);
        });
}

// ── chunked body state machine ──────────────────────────────────────
//
// HTTP/1.1 chunked transfer-encoding (RFC 7230 §4.1):
//
//   chunked-body   = *chunk
//                    last-chunk
//                    trailer-part
//                    CRLF
//   chunk          = chunk-size [ chunk-ext ] CRLF chunk-data CRLF
//   last-chunk     = 1*("0") [ chunk-ext ] CRLF
//   trailer-part   = *( header-field CRLF )
//
// We ignore chunk-ext and discard any trailer headers. acc accumulates the
// concatenated chunk-data (no CRLFs).

void Connection::read_chunked_body(std::shared_ptr<HttpRequest> request,
                                   std::shared_ptr<std::string> acc) {
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n",
        [this, self, request, acc](const boost::system::error_code& ec, std::size_t n) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                FOX_HTTP_LOG("Connection(" << id() << ")::chunked size line - "
                             << ec.message());
                socket_.close();
                return;
            }

            auto begin = asio::buffers_begin(request_buffer_.data());
            std::string line(begin, begin + (n - 2));  // strip trailing \r\n
            request_buffer_.consume(n);

            // Strip chunk-extensions (";ext=...") and surrounding whitespace.
            if (auto semi = line.find(';'); semi != std::string::npos) line.resize(semi);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());

            std::size_t chunk_size = 0;
            try {
                chunk_size = std::stoul(line, nullptr, 16);
            } catch (...) {
                FOX_HTTP_LOG("Connection(" << id() << ")::chunked size parse failed: '" << line << "'");
                is_processing_.store(false, std::memory_order_relaxed);
                socket_.close();
                return;
            }

            if (chunk_size == 0) {
                read_chunk_trailers(std::move(request), std::move(acc));
            } else {
                read_chunk_data(std::move(request), std::move(acc), chunk_size);
            }
        });
}

void Connection::read_chunk_data(std::shared_ptr<HttpRequest> request,
                                 std::shared_ptr<std::string> acc,
                                 std::size_t chunk_size) {
    // Take whatever data bytes are already in the streambuf (up to chunk_size).
    std::size_t have = request_buffer_.size();
    std::size_t to_take_from_buf = std::min(have, chunk_size);
    if (to_take_from_buf > 0) {
        auto begin = asio::buffers_begin(request_buffer_.data());
        acc->append(begin, begin + to_take_from_buf);
        request_buffer_.consume(to_take_from_buf);
    }

    std::size_t remaining = chunk_size - to_take_from_buf;
    if (remaining == 0) {
        read_chunk_trailing_crlf(std::move(request), std::move(acc));
        return;
    }

    // Read the remaining data bytes directly into acc (pre-resize, no copy).
    std::size_t offset = acc->size();
    acc->resize(offset + remaining);
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(acc->data() + offset, remaining),
        asio::transfer_exactly(remaining),
        [this, self, request, acc](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                FOX_HTTP_LOG("Connection(" << id() << ")::chunk data - " << ec.message());
                socket_.close();
                return;
            }
            read_chunk_trailing_crlf(std::move(request), std::move(acc));
        });
}

void Connection::read_chunk_trailing_crlf(std::shared_ptr<HttpRequest> request,
                                          std::shared_ptr<std::string> acc) {
    // Each chunk ends with CRLF (2 bytes) after its data.
    if (request_buffer_.size() >= 2) {
        request_buffer_.consume(2);
        read_chunked_body(std::move(request), std::move(acc));
        return;
    }
    std::size_t need = 2 - request_buffer_.size();
    request_buffer_.consume(request_buffer_.size());
    auto dummy = std::make_shared<std::array<char, 2>>();
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(dummy->data(), need),
        asio::transfer_exactly(need),
        [this, self, request, acc, dummy](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                FOX_HTTP_LOG("Connection(" << id() << ")::chunk CRLF - " << ec.message());
                socket_.close();
                return;
            }
            read_chunked_body(std::move(request), std::move(acc));
        });
}

void Connection::read_chunk_trailers(std::shared_ptr<HttpRequest> request,
                                     std::shared_ptr<std::string> acc) {
    // After the zero-size chunk, zero or more trailer header lines terminated
    // by CRLF, then a final empty line. We drop trailers on the floor.
    auto self(shared_from_this());
    asio::async_read_until(socket_, request_buffer_, "\r\n",
        [this, self, request, acc](const boost::system::error_code& ec, std::size_t n) {
            if (ec) {
                is_processing_.store(false, std::memory_order_relaxed);
                FOX_HTTP_LOG("Connection(" << id() << ")::chunk trailer - " << ec.message());
                socket_.close();
                return;
            }
            bool empty_line = (n == 2);
            request_buffer_.consume(n);
            if (empty_line) {
                request->set_body(std::move(*acc));
                dispatch(std::move(request));
            } else {
                read_chunk_trailers(std::move(request), std::move(acc));
            }
        });
}

// ── dispatch / modes ────────────────────────────────────────────────

void Connection::dispatch(std::shared_ptr<HttpRequest> request) {
    // Keep the response on the stack for the common Buffered/Immediate paths
    // to avoid a per-request heap allocation. Only Stream mode needs to outlive
    // this frame, in which case we move the response into a shared_ptr below.
    HttpResponse response;
    response.attach_stream(socket_stream_.get());

    auto conn_hdr = request->header("Connection");
    if (conn_hdr.empty() || conn_hdr == "close") {
        keep_alive_ = false;
    } else {
        keep_alive_ = true;
    }
    response.headers().insert("Connection", keep_alive_ ? "keep-alive" : "close");

    // Exception safety: a throwing handler must not propagate out to the
    // io_context — that would crash the io thread. If the handler throws
    // before flushing headers we still have the opportunity to produce a
    // 500; otherwise we log and close the connection.
    std::string caught_msg;
    bool caught = false;
    try {
        handler_.handle(*request, response);
    } catch (const std::exception& e) {
        caught = true;
        caught_msg = e.what();
    } catch (...) {
        caught = true;
        caught_msg = "unknown exception";
    }

    if (caught) {
        FOX_HTTP_LOG("handler threw: " << caught_msg);
        if (!response.headers_flushed()) {
            replace_with_500(response, caught_msg);
            write_buffered(response);
        } else {
            // Headers already on the wire — nothing clean we can do, close.
            keep_alive_ = false;
            finish_request();
        }
        return;
    }

    switch (response.mode()) {
        case HttpResponse::Mode::Buffered:
            write_buffered(response);
            return;

        case HttpResponse::Mode::Immediate:
            // Handler wrote everything synchronously. If no Content-Length was
            // set, we can't safely keep-alive (client can't frame the body).
            if (!response.headers().contains("Content-Length") &&
                !response.headers().contains("Transfer-Encoding")) {
                keep_alive_ = false;
            }
            finish_request();
            return;

        case HttpResponse::Mode::Stream: {
            auto resp_heap = std::make_shared<HttpResponse>(std::move(response));
            resp_heap->attach_stream(socket_stream_.get());
            post_stream_work(std::move(request), std::move(resp_heap));
            return;
        }
    }
}

void Connection::replace_with_500(HttpResponse& response, std::string_view msg) {
    // Rebuild the response from scratch. Headers haven't been sent yet.
    response = HttpResponse{};
    response.attach_stream(socket_stream_.get());
    response.set_status(500);
    response.headers().content_type("text/plain; charset=utf-8");
    response.headers().insert("Connection", "close");
    std::string body = "500 Internal Server Error\n";
    if (!msg.empty()) {
        body.append(msg);
        body.push_back('\n');
    }
    response.set_body(std::move(body));
    keep_alive_ = false;
}

void Connection::write_buffered(HttpResponse& response) {
    auto payload = std::make_shared<std::string>(response.serialize());
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(*payload),
        [this, self, payload](const boost::system::error_code& ec, std::size_t /*n*/) {
            is_processing_.store(false, std::memory_order_relaxed);
            if (ec) {
                FOX_HTTP_LOG("Connection(" << id() << ")::write_buffered() - "
                               << ec.message());
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

void Connection::post_stream_work(std::shared_ptr<HttpRequest> request,
                                  std::shared_ptr<HttpResponse> response) {
    auto self(shared_from_this());
    dispatcher_.post([self, request, response]() {
        std::string caught_msg;
        bool caught = false;
        try {
            auto& fn = response->stream_fn();
            if (fn) fn(*response);
            response->end_chunks();
        } catch (const std::exception& e) {
            caught = true;
            caught_msg = e.what();
        } catch (...) {
            caught = true;
            caught_msg = "unknown exception";
        }

        if (caught) {
            FOX_HTTP_LOG("stream lambda threw: " << caught_msg);
            if (!response->headers_flushed()) {
                // Headers not flushed yet — synthesize a 500 reply.
                self->replace_with_500(*response, caught_msg);
                // We're on a handler-pool thread; send synchronously via the
                // attached stream to keep the logic simple.
                auto payload = response->serialize();
                response->attach_stream(self->socket_stream_.get());
                self->socket_stream_->write(payload.data(), payload.size());
            }
            self->keep_alive_ = false;
        }

        self->socket_.get_executor().execute([self]() {
            self->finish_request();
        });
    });
}

void Connection::finish_request() {
    is_processing_.store(false, std::memory_order_relaxed);
    last_active_time_ = std::chrono::system_clock::now();
    if (keep_alive_) {
        read();
    } else if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
    }
}

}  // namespace fox::http
