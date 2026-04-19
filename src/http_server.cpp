#include "fox-http/http_server.h"

#include "connection.h"
#include "fox-http/http_handler.h"
#include "logger.h"
#include "response_stream.h"
#include "timer_manager.h"

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace fox::http {

namespace asio = boost::asio;
using asio::ip::tcp;

struct HttpServer::Impl : StreamDispatcher {
    Impl(unsigned short port, std::size_t io_threads)
        : port_(port),
          io_threads_(io_threads == 0 ? std::thread::hardware_concurrency() : io_threads),
          acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {
        if (io_threads_ == 0) io_threads_ = 1;
    }

    // StreamDispatcher: lazily create the handler pool on first use.
    void post(std::function<void()> fn) override {
        std::call_once(stream_pool_init_, [this]() {
            std::size_t n = stream_pool_size_;
            if (n == 0) n = 4 * std::thread::hardware_concurrency();
            if (n == 0) n = 4;
            stream_pool_ = std::make_unique<asio::thread_pool>(n);
            FOX_HTTP_LOG("handler thread pool started with " << n << " threads");
        });
        asio::post(*stream_pool_, std::move(fn));
    }

    int run() {
        if (!handler_) {
            FOX_HTTP_LOG("HttpServer::run() - no handler set");
            return 1;
        }

        auto& tm = TimerManager::instance();
        tm.init(io_context_, std::chrono::seconds(3));
        tm.start();

        asio::signal_set signals(io_context_, SIGINT, SIGTERM);
#ifdef SIGTSTP
        signals.add(SIGTSTP);
#endif
        signals.async_wait([this](const boost::system::error_code&, int) { stop(); });

        accept();

        std::vector<std::thread> pool;
        pool.reserve(io_threads_);
        FOX_HTTP_LOG("HttpServer listening on port " << port_
                       << " with " << io_threads_ << " io threads");
        for (std::size_t i = 0; i < io_threads_; ++i) {
            pool.emplace_back([this]() { io_context_.run(); });
        }
        for (auto& t : pool) {
            if (t.joinable()) t.join();
        }

        if (stream_pool_) {
            stream_pool_->stop();
            stream_pool_->join();
        }
        return 0;
    }

    void stop() {
        TimerManager::instance().stop();
        boost::system::error_code ec;
        acceptor_.close(ec);
        io_context_.stop();
    }

    void accept() {
        auto conn = std::make_shared<Connection>(io_context_, *handler_, *this);
        acceptor_.async_accept(conn->socket(),
            [this, conn](const boost::system::error_code& ec) {
                if (!ec) {
                    TimerManager::instance().add_connection(conn, conn->id());
                    conn->start();
                }
                if (acceptor_.is_open()) accept();
            });
    }

    unsigned short port_;
    std::size_t io_threads_;
    asio::io_context io_context_;
    tcp::acceptor acceptor_;
    HttpHandler* handler_ = nullptr;

    std::size_t stream_pool_size_ = 0;  // 0 = default (4 * hw_concurrency)
    std::once_flag stream_pool_init_;
    std::unique_ptr<asio::thread_pool> stream_pool_;
};

HttpServer::HttpServer(unsigned short port, std::size_t io_threads)
    : impl_(std::make_unique<Impl>(port, io_threads)) {}

HttpServer::~HttpServer() = default;

void HttpServer::set_handler(HttpHandler* handler) {
    impl_->handler_ = handler;
}

void HttpServer::set_stream_pool_size(std::size_t n) {
    impl_->stream_pool_size_ = n;
}

int HttpServer::run() {
    return impl_->run();
}

void HttpServer::stop() {
    impl_->stop();
}

}  // namespace fox::http
