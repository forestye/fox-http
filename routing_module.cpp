/*
实现了RoutingModule类的方法。
register_handler()方法接受一个路径、一个HTTP方法和一个请求处理函数作为参数，将处理函数注册到handlers_映射中。

route_request()方法根据请求的路径和方法查找相应的处理函数。
如果找到处理函数，我们调用它并传入请求和响应对象；如果没有找到处理函数，我们返回一个404 Not Found响应。
*/

#include "routing_module.h"
using namespace std;

void RoutingModule::register_handler(const std::string &path, const HttpRequest::Method &method, const RequestHandler &handler) {
	handlers_[path][method] = handler;
}

void RoutingModule::route_request(const HttpRequest &request, HttpResponse &response) {
	// 在处理程序映射中查找请求路径
	auto it = handlers_.find(request.get_path());
	// 如果找到了路径
	if (it != handlers_.end()) {
		// 在找到的路径的处理程序映射中查找请求方法
		auto method_it = it->second.find(request.get_method());
		// 如果找到了方法
		if (method_it != it->second.end()) {
			// 调用对应的处理程序
			(*(method_it->second))(request, response);
		} else {
			// 如果没有找到方法，设置响应状态码为405（方法不允许）
			response.set_status_code(405);
			response.set_reason_phrase("Method Not Allowed");
			// 设置Allow响应头，列出该资源支持的方法
			std::string allow_methods;
			for (const auto& method_handler : it->second) {
				allow_methods += HttpRequest::MethodToString(method_handler.first) + ", ";
			}
			// 移除最后的逗号和空格
			allow_methods = allow_methods.substr(0, allow_methods.size()-2);
			// 设置Allow响应头
			response.set_header("Allow", allow_methods);
		}
	} else {
		// 如果没有找到路径，设置响应状态码为404（未找到）
		response.set_status_code(404);
		response.set_reason_phrase("Not Found");
	}
}