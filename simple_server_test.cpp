#include <iostream>
#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include "server.h"
#include "routing_module.h"
#include "server_status.h"

using namespace std;
using namespace boost;

class HelloHandler : public BaseRequestHandler {
public:
    void operator()(const HttpRequest &request, HttpResponse &response) override {
        auto params=request.get_query_params();
        string param_str="param count="+lexical_cast<string>(params.size())+"<br>";
        param_str+="a="+params["a"]+"<br>";
        param_str+="b="+params["b"]+"<br>";
        set_common_response_headers(response);
        std::string body = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Hello</title>
            </head>
            <body>
                <h1>Welcome to the Simple HTTP Server!</h1>
            )"+param_str+R"(
            </body>
            </html>
        )";        
        response.set_body(body);
    }
};

class ServerStatusHandler : public BaseRequestHandler {
public:
    void operator()(const HttpRequest &request, HttpResponse &response) override {
        set_common_response_headers(response);
		string status_str=ServerStatus::instance().get_status_str();
        std::string body = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>System Status</title>
            </head>
            <body>
                <h1>HTTP Server Status</h1>
            )"+status_str+R"(
            </body>
            </html>
        )";        
        response.set_body(body);
    }
};


int main(int argc, char* argv[]) {
    unsigned int num_threads=0;
    if(argc>1) {
        num_threads=lexical_cast<unsigned int>(argv[1]);
    }
    unsigned short port = 8080;
    RoutingModule routing_module;
    routing_module.register_handler("/hello", HttpRequest::Method::GET, std::make_shared<HelloHandler>());
	routing_module.register_handler("/status", HttpRequest::Method::GET, std::make_shared<ServerStatusHandler>());

    auto server = std::make_shared<Server>(port, routing_module);

    // Signal handling for graceful shutdown
    boost::asio::io_context signal_io_context;
    boost::asio::signal_set signals(signal_io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& error, int signal_number) {
        if (!error) {
            std::cout << "Shutting down the server..." << std::endl;
            server->stop();
        }
    });
    std::thread signal_thread([&]() { signal_io_context.run(); });

    try {
        server->run(num_threads);
        std::cout << "---Server stopped---" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        server->stop();
        signal_thread.join();
        return 1;
    }

    signal_thread.join();
    return 0;
}
