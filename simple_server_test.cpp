#include <iostream>
#include <string>
#include <boost/lexical_cast.hpp>
#include "server.h"
#include "routing_module.h"
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
                <h1>欢迎来到简单的HTTP服务器！</h1>
            )"+param_str+R"(
            </body>
            </html>
        )";
        
        response.set_body(body);
    }
};


int main() {
    unsigned short port = 8080;
    RoutingModule routing_module;
    routing_module.register_handler("/hello", HttpRequest::Method::GET, std::make_shared<HelloHandler>());

    auto server = std::make_shared<Server>(port, routing_module);
    try {
        server->run();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
