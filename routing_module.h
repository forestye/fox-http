/*
Defines RoutingModule class for routing HTTP requests to appropriate handlers.
RoutingModule uses std::map to store registered request handlers, mapping std::pair<std::string, HttpRequest::Method> to handler functions.
Handler functions are std::function objects accepting HttpRequest and HttpResponse parameters.
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
	std::map<std::string, std::map<HttpRequest::Method, std::shared_ptr<BaseRequestHandler>>> handlers_;
};

#endif // ROUTING_MODULE_H
