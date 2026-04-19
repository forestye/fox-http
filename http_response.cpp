/*
Implements HttpResponse class methods.
- set_status_code(), set_reason_phrase(), set_header(), and set_body(): Set response properties.
- to_string(): Converts response to a string for client transmission.
*/
#include "http_response.h"
#include <sstream>

void HttpResponse::set_status_code(int status_code) {
    status_code_ = status_code;
}

void HttpResponse::set_reason_phrase(const std::string& reason_phrase) {
    reason_phrase_ = reason_phrase;
}

void HttpResponse::set_header(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

void HttpResponse::set_body(const std::string& body) {
    body_ = body;
}

std::string HttpResponse::to_string() const {
    std::ostringstream ss;

    // Write the status line
    ss << "HTTP/1.1 " << status_code_ << " " << reason_phrase_ << "\r\n";

    // Write headers
    for (const auto& header : headers_) {
        ss << header.first << ": " << header.second << "\r\n";
    }

    // Write the Content-Length header
    ss << "Content-Length: " << body_.length() << "\r\n";

    // End of headers
    ss << "\r\n";

    // Write the response body
    ss << body_;

    return ss.str();
}
