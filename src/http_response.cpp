#include "httpserver/http_response.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace httpserver {

namespace {

bool ci_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ── Headers ────────────────────────────────────────────────────────

std::vector<HttpResponse::Headers::Entry>::iterator
HttpResponse::Headers::find_ci(std::string_view name) {
    return std::find_if(entries_.begin(), entries_.end(),
        [&](const Entry& e) { return ci_equal(e.name, name); });
}

std::vector<HttpResponse::Headers::Entry>::const_iterator
HttpResponse::Headers::find_ci(std::string_view name) const {
    return std::find_if(entries_.begin(), entries_.end(),
        [&](const Entry& e) { return ci_equal(e.name, name); });
}

HttpResponse::Headers& HttpResponse::Headers::insert(std::string_view name, std::string_view value) {
    auto it = find_ci(name);
    if (it != entries_.end()) {
        it->value.assign(value);
    } else {
        entries_.push_back({std::string(name), std::string(value)});
    }
    return *this;
}

HttpResponse::Headers& HttpResponse::Headers::content_length(std::size_t n) {
    return insert("Content-Length", std::to_string(n));
}

HttpResponse::Headers& HttpResponse::Headers::content_type(std::string_view ct) {
    return insert("Content-Type", ct);
}

std::string_view HttpResponse::Headers::get(std::string_view name) const {
    auto it = find_ci(name);
    if (it == entries_.end()) return {};
    return it->value;
}

bool HttpResponse::Headers::contains(std::string_view name) const {
    return find_ci(name) != entries_.end();
}

// ── HttpResponse ───────────────────────────────────────────────────

void HttpResponse::set_body(std::string body) {
    body_ = std::move(body);
}

std::string_view HttpResponse::reason_phrase(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 418: return "I'm a teapot";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "";
    }
}

std::string HttpResponse::serialize() const {
    std::string out;
    out.reserve(128 + body_.size());

    out.append("HTTP/1.1 ");
    out.append(std::to_string(status_));
    out.append(" ");
    out.append(reason_phrase(status_));
    out.append("\r\n");

    bool has_content_length = headers_.contains("Content-Length");
    for (const auto& e : headers_.entries_) {
        out.append(e.name);
        out.append(": ");
        out.append(e.value);
        out.append("\r\n");
    }
    if (!has_content_length) {
        out.append("Content-Length: ");
        out.append(std::to_string(body_.size()));
        out.append("\r\n");
    }
    out.append("\r\n");
    out.append(body_);
    return out;
}

}  // namespace httpserver
