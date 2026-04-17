#include "httpserver/http_handler.h"
#include "httpserver/http_request.h"
#include "httpserver/http_response.h"
#include "httpserver/http_router.h"
#include "httpserver/http_server.h"
#include "httpserver/http_util.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace httpserver;

int main(int argc, char* argv[]) {
    unsigned short port = 8080;
    std::size_t io_threads = 0;
    if (argc > 1) port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (argc > 2) io_threads = static_cast<std::size_t>(std::atoi(argv[2]));

    HttpRouter router;

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

    HttpServer server(port, io_threads);
    server.set_handler(&router);
    std::cout << "hello_world listening on port " << port << std::endl;
    return server.run();
}
