#include "httpserver/http_handler.h"
#include "httpserver/http_request.h"
#include "httpserver/http_response.h"
#include "httpserver/http_router.h"
#include "httpserver/http_server.h"
#include "httpserver/http_util.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/uio.h>
#include <thread>

using namespace httpserver;

int main(int argc, char* argv[]) {
    unsigned short port = 8080;
    std::size_t io_threads = 0;
    if (argc > 1) port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (argc > 2) io_threads = static_cast<std::size_t>(std::atoi(argv[2]));

    HttpRouter router;

    // Buffered mode — the typical short API.
    router.get("/hello", [](HttpRequest&, HttpResponse& resp) {
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body("hello\n");
    });

    router.get("/user/:id", [](HttpRequest& req, HttpResponse& resp) {
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body("user id = " + std::string(req.param("id")) + "\n");
    });

    router.get("/files/*path", [](HttpRequest& req, HttpResponse& resp) {
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body("path = " + std::string(req.param("path")) + "\n");
    });

    router.post("/echo", [](HttpRequest& req, HttpResponse& resp) {
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body(std::string(req.body()));
    });

    // Immediate / writev mode — demonstrates zero-copy scatter-gather send.
    // Static fragments are compile-time string literals; the dynamic fragment
    // lives on the handler stack (valid for the duration of the sync writev).
    router.get("/weave/:name", [](HttpRequest& req, HttpResponse& resp) {
        static const char part1[] = "<html><body><h1>hello ";
        static const char part2[] = "</h1></body></html>";
        std::string name(req.param("name"));
        std::size_t total = (sizeof(part1) - 1) + name.size() + (sizeof(part2) - 1);
        resp.headers().content_type("text/html; charset=utf-8");
        resp.headers().content_length(total);

        struct iovec iov[3];
        iov[0].iov_base = const_cast<char*>(part1); iov[0].iov_len = sizeof(part1) - 1;
        iov[1].iov_base = const_cast<char*>(name.data()); iov[1].iov_len = name.size();
        iov[2].iov_base = const_cast<char*>(part2); iov[2].iov_len = sizeof(part2) - 1;
        resp.writev(iov, 3);
    });

    // Stream mode — chunked slow drip.
    router.get("/slow", [](HttpRequest&, HttpResponse& resp) {
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.stream([](HttpResponse& r) {
            for (int i = 0; i < 5; ++i) {
                std::string line = "chunk " + std::to_string(i) + "\n";
                r.write_chunk(line);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    });

    // Stream mode — Server-Sent Events.
    router.get("/sse", [](HttpRequest&, HttpResponse& resp) {
        resp.headers().content_type("text/event-stream");
        resp.headers().insert("Cache-Control", "no-cache");
        resp.stream([](HttpResponse& r) {
            for (int i = 0; i < 10; ++i) {
                r.write_sse_event("message", "tick " + std::to_string(i));
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
    });

    HttpServer server(port, io_threads);
    server.set_handler(&router);
    std::cout << "hello_world listening on port " << port << std::endl;
    return server.run();
}
