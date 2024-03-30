/*
Implements RoutingModule class methods.
- register_handler(): Registers a request handler function for a given path and HTTP method in handlers_ map.
- route_request(): Finds and calls the handler function for the request's path and method. If no handler is found, returns a 404 Not Found response. If path is found but method is not allowed, returns a 405 Method Not Allowed response.
*/
#include "routing_module.h"
using namespace std;

void RoutingModule::register_handler(const std::string &path, const HttpRequest::Method &method, const RequestHandler &handler) {
	handlers_[path][method] = handler;
}

void RoutingModule::route_request(const HttpRequest &request, HttpResponse &response) {
	auto it = handlers_.find(request.get_path());
	if (it != handlers_.end()) {
		auto method_it = it->second.find(request.get_method());
		if (method_it != it->second.end()) {
			(*(method_it->second))(request, response);
		} else {
			response.set_status_code(405);
			response.set_reason_phrase("Method Not Allowed");
			std::string allow_methods;
			for (const auto& method_handler : it->second) {
				allow_methods += HttpRequest::MethodToString(method_handler.first) + ", ";
			}
			allow_methods = allow_methods.substr(0, allow_methods.size()-2);
			response.set_header("Allow", allow_methods);
		}
	} else {
		response.set_status_code(404);
		response.set_reason_phrase("Not Found");
	}
}
