#include "httpserver/http_server.h"

#include "connection.h"
#include "httpserver/http_handler.h"
#include "logger.h"
#include "timer_manager.h"

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <chrono>
#include <thread>
#include <vector>

namespace httpserver {

namespace asio = boost::asio;
using asio::ip::tcp;

struct HttpServer::Impl {
    Impl(unsigned short port, std::size_t io_threads)
        : port_(port),
          io_threads_(io_threads == 0 ? std::thread::hardware_concurrency() : io_threads),
          acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {
        if (io_threads_ == 0) io_threads_ = 1;
    }

    int run() {
        if (!handler_) {
            HTTPSERVER_LOG("HttpServer::run() - no handler set");
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
        HTTPSERVER_LOG("HttpServer listening on port " << port_
                       << " with " << io_threads_ << " io threads");
        for (std::size_t i = 0; i < io_threads_; ++i) {
            pool.emplace_back([this]() { io_context_.run(); });
        }
        for (auto& t : pool) {
            if (t.joinable()) t.join();
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
        auto conn = std::make_shared<Connection>(io_context_, *handler_);
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
};

HttpServer::HttpServer(unsigned short port, std::size_t io_threads)
    : impl_(std::make_unique<Impl>(port, io_threads)) {}

HttpServer::~HttpServer() = default;

void HttpServer::set_handler(HttpHandler* handler) {
    impl_->handler_ = handler;
}

int HttpServer::run() {
    return impl_->run();
}

void HttpServer::stop() {
    impl_->stop();
}

}  // namespace httpserver
