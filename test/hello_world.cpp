#include "httpserver/http_handler.h"
#include "httpserver/http_request.h"
#include "httpserver/http_response.h"
#include "httpserver/http_server.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace httpserver;

class HelloHandler : public HttpHandler {
public:
    void handle(HttpRequest& req, HttpResponse& resp) override {
        if (req.path() == "/hello") {
            resp.set_status(200);
            resp.headers().content_type("text/plain; charset=utf-8");
            resp.set_body("hello\n");
            return;
        }
        resp.set_status(404);
        resp.headers().content_type("text/plain");
        resp.set_body("not found\n");
    }
};

int main(int argc, char* argv[]) {
    unsigned short port = 8080;
    std::size_t io_threads = 0;  // 0 = hardware_concurrency
    if (argc > 1) port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (argc > 2) io_threads = static_cast<std::size_t>(std::atoi(argv[2]));

    HelloHandler handler;
    HttpServer server(port, io_threads);
    server.set_handler(&handler);

    std::cout << "hello_world listening on port " << port << std::endl;
    return server.run();
}
