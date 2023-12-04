/*
定义了一个名为HttpRequest的类，它表示一个HTTP请求。
HttpRequest类提供了用于解析请求的方法，以及用于访问请求的属性（如方法、路径、版本和头字段）的方法。我们使用std::map来存储请求头字段。
*/
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <iostream>
#include <map>
#include <string>

class HttpRequest {
public:
    enum class Method { GET, POST, PUT, DELETE, OPTIONS, UNKNOWN };

	static std::string MethodToString(Method method) {
		switch (method) {
			case Method::GET: return "GET";
			case Method::POST: return "POST";
			case Method::PUT: return "PUT";
			case Method::DELETE: return "DELETE";
			case Method::OPTIONS: return "OPTIONS";
			case Method::UNKNOWN: return "UNKNOWN";
			default: return "UNKNOWN";
		}
	}

    // Parse the request from the input stream
    void parse(std::istream& input);

    // Get the request method
    Method get_method() const;

    // Get the request path
    const std::string& get_path() const;

    // Get the HTTP version
    const std::string& get_version() const;

    // Get the value of a header field by its name
    const std::string& get_header(const std::string& name) const;

    const std::string& get_body() const;

    const std::map<std::string, std::string>& get_query_params() const;

private:
    // Helper method to parse the request line
    void parse_request_line(const std::string& line);

    // Helper method to parse headers
    void parse_headers(std::istream& input);

    void parse_body(std::istream& input);

    void trim(std::string& str);

    bool is_valid_header(const std::string& name);

    // The request method
    Method method_;

    // The request path
    std::string path_;

    // The HTTP version
    std::string version_;

    // The request headers
    std::map<std::string, std::string> headers_;

    std::string body_;
    
    void parse_query_string(const std::string& query_string);
    void url_decode(const std::string& in, std::string& out);

    std::map<std::string, std::string> query_params_;
};

#endif // HTTP_REQUEST_H
