#pragma once

#include <cstddef>
#include <memory>

namespace fox::http {

class HttpHandler;

class HttpServer {
public:
    // port: TCP port to listen on.
    // io_threads: size of io_context thread pool; 0 means hardware_concurrency().
    explicit HttpServer(unsigned short port, std::size_t io_threads = 0);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Register the single root handler. Handler lifetime is caller-managed
    // and must outlive run().
    void set_handler(HttpHandler* handler);

    // Size of the lazy-init handler thread pool used for Stream-mode
    // responses. 0 means default (4 * hardware_concurrency). No threads are
    // created until the first stream response runs.
    void set_stream_pool_size(std::size_t n);

    // Blocks until SIGINT / SIGTERM / SIGTSTP is received. Returns 0 on
    // clean shutdown, non-zero on startup failure.
    int run();

    // Request a shutdown from another thread.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fox::http
