/*
实现了RoutingModule类的方法。
register_handler()方法接受一个路径、一个HTTP方法和一个请求处理函数作为参数，将处理函数注册到handlers_映射中。

route_request()方法根据请求的路径和方法查找相应的处理函数。
如果找到处理函数，我们调用它并传入请求和响应对象；如果没有找到处理函数，我们返回一个404 Not Found响应。
*/

#include "routing_module.h"

// Register a request handler for a specific path and method
void RoutingModule::register_handler(const std::string &path, const HttpRequest::Method &method, const RequestHandler &handler) {
    handlers_[std::make_pair(path, method)] = handler;
}

// Route the request to the appropriate handler and fill the response
void RoutingModule::route_request(const HttpRequest &request, HttpResponse &response) {
    auto it = handlers_.find(std::make_pair(request.get_path(), request.get_method()));
    if (it != handlers_.end()) {
        (*(it->second))(request, response);
    } else {
        response.set_status_code(404);
        response.set_reason_phrase("Not Found");
    }
}
