/*
Implements HttpRequest class methods.
- parse(): Reads and parses request line and header fields from an input stream.
- parse_request_line() and parse_headers(): Helper methods for parsing request line and header fields respectively.
- get_method(), get_path(), get_version(), and get_header(): Accessors for request properties.
*/
#include "http_request.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <map>
#include <istream>
using namespace std;

void HttpRequest::parse(std::istream& input) {
    std::string line;

    // Parse request line
    std::getline(input, line);
    parse_request_line(line);

    // Parse headers
    parse_headers(input);

    // Parse body (if any)
    parse_body(input);}

HttpRequest::Method HttpRequest::get_method() const {
    return method_;
}

const std::string& HttpRequest::get_path() const {
    return path_;
}

const std::string& HttpRequest::get_version() const {
    return version_;
}

const std::string& HttpRequest::get_header(const std::string& name) const {
    static const std::string empty;
    auto it = headers_.find(name);
    return it != headers_.end() ? it->second : empty;
}

void HttpRequest::parse_request_line(const std::string& line) {
    std::istringstream ss(line);
    std::string method_str;

    ss >> method_str >> path_ >> version_;

	// 把method_str转成大写
	std::transform(method_str.begin(), method_str.end(), method_str.begin(), ::toupper);

    if (method_str == "GET") {
        method_ = Method::GET;
    } else if (method_str == "POST") {
        method_ = Method::POST;
    } else if (method_str == "PUT") {
        method_ = Method::PUT;
    } else if (method_str == "DELETE") {
        method_ = Method::DELETE;
	} else if (method_str == "OPTIONS") {
		method_ = Method::OPTIONS;
    } else {
        method_ = Method::UNKNOWN;
    }

    // Parse query parameters
    std::size_t query_pos = path_.find('?');
    if (query_pos != std::string::npos) {
        std::string query_string = path_.substr(query_pos + 1);
        path_ = path_.substr(0, query_pos);
        parse_query_string(query_string);
    }

}


const std::string& HttpRequest::get_body() const {
    return body_;
}

void HttpRequest::parse_headers(std::istream& input) {
    std::string line;
    while (std::getline(input, line) && line != "\r") {
        std::size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            trim(name);
            trim(value);
            if (is_valid_header(name)) {
                headers_[name] = value;
            }
        }
    }
}

void HttpRequest::parse_body(std::istream& input) {
    auto it = headers_.find("Content-Length");
    if (it != headers_.end()) {
        int content_length = std::stoi(it->second);
        body_.resize(content_length);
        input.read(&body_[0], content_length);
    }
}

bool HttpRequest::is_valid_header(const std::string& name) {
    // basic implementation
    return !name.empty() && std::isalpha(name[0]);
}

void HttpRequest::trim(std::string& str) {
    auto front = std::find_if_not(str.begin(), str.end(), [](int c) { return std::isspace(c); });
    str.erase(str.begin(), front);
    auto back = std::find_if_not(str.rbegin(), str.rend(), [](int c) { return std::isspace(c); }).base();
    str.erase(back, str.end());
}

const std::map<std::string, std::string>& HttpRequest::get_query_params() const {
    return query_params_;
}


void HttpRequest::parse_query_string(const std::string& query_string) {
    std::istringstream ss(query_string);
    std::string token;

    while (std::getline(ss, token, '&')) {
        std::size_t equal_pos = token.find('=');
        if (equal_pos != std::string::npos) {
            std::string key = token.substr(0, equal_pos);
            std::string value = token.substr(equal_pos + 1);
            std::string decoded_key, decoded_value;
            url_decode(key, decoded_key);
            url_decode(value, decoded_value);
            query_params_[decoded_key] = decoded_value;
        }
    }
}

void HttpRequest::url_decode(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 2 < in.size()) {
                int value = 0;
                std::istringstream is(in.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    out += static_cast<char>(value);
                    i += 2;
                } else {
                    out += '%';
                }
            } else {
                out += '%';
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
}