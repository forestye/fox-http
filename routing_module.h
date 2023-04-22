/*
定义了一个名为RoutingModule的类，它负责根据请求的路径和方法查找并调用适当的请求处理函数。
我们使用std::map来存储注册的请求处理函数，
它将std::pair<std::string, HttpRequest::Method>映射到处理函数。
处理函数是一个接受HttpRequest和HttpResponse参数的std::function对象。
*/
#ifndef ROUTING_MODULE_H
#define ROUTING_MODULE_H

#include <functional>
#include <map>
#include <string>
#include <memory>
#include "http_request.h"
#include "http_response.h"
#include "request_handler.h" 

class RoutingModule {
public:
    // Type alias for a request handler object
    using RequestHandler = std::shared_ptr<BaseRequestHandler>;

    // Register a request handler for a specific path and method
    void register_handler(const std::string &path, const HttpRequest::Method &method, const RequestHandler &handler);

    // Route the request to the appropriate handler and fill the response
    void route_request(const HttpRequest &request, HttpResponse &response);

private:
    // Map to store the registered request handlers
    std::map<std::pair<std::string, HttpRequest::Method>, RequestHandler> handlers_;
};

#endif // ROUTING_MODULE_H
