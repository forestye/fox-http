#include "fox-http/http_request.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>

namespace fox::http {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string url_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            int value = 0;
            auto [p, ec] = std::from_chars(in.data() + i + 1, in.data() + i + 3, value, 16);
            if (ec == std::errc{} && p == in.data() + i + 3) {
                out += static_cast<char>(value);
                i += 2;
            } else {
                out += '%';
            }
        } else if (c == '+') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

bool HttpRequest::CiLess::operator()(const std::string& a, const std::string& b) const noexcept {
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

std::string_view HttpRequest::method_to_string(Method m) {
    switch (m) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::DELETE:  return "DELETE";
        case Method::PATCH:   return "PATCH";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        default:              return "UNKNOWN";
    }
}

HttpRequest::Method HttpRequest::method_from_string(std::string_view s) {
    if (s == "GET")     return Method::GET;
    if (s == "POST")    return Method::POST;
    if (s == "PUT")     return Method::PUT;
    if (s == "DELETE")  return Method::DELETE;
    if (s == "PATCH")   return Method::PATCH;
    if (s == "HEAD")    return Method::HEAD;
    if (s == "OPTIONS") return Method::OPTIONS;
    return Method::UNKNOWN;
}

std::string_view HttpRequest::method_str() const {
    return method_to_string(method_);
}

bool HttpRequest::parse_header(std::string_view block) {
    // Split on the first CRLF.
    auto eol = block.find("\r\n");
    if (eol == std::string_view::npos) return false;
    parse_request_line(block.substr(0, eol));

    // Remaining is headers terminated by \r\n\r\n (or EOF).
    auto rest = block.substr(eol + 2);
    // Strip the trailing \r\n\r\n if present.
    if (auto end = rest.find("\r\n\r\n"); end != std::string_view::npos) {
        rest = rest.substr(0, end);
    }
    parse_headers(rest);
    return true;
}

void HttpRequest::parse_request_line(std::string_view line) {
    auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return;
    auto sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return;

    std::string method_str(line.substr(0, sp1));
    for (auto& c : method_str) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    method_ = method_from_string(method_str);

    std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    version_ = std::string(line.substr(sp2 + 1));
    // Strip trailing \r if present.
    if (!version_.empty() && version_.back() == '\r') version_.pop_back();

    auto qpos = target.find('?');
    if (qpos != std::string_view::npos) {
        path_ = std::string(target.substr(0, qpos));
        parse_query_string(target.substr(qpos + 1));
    } else {
        path_ = std::string(target);
    }
}

void HttpRequest::parse_headers(std::string_view block) {
    std::size_t pos = 0;
    while (pos < block.size()) {
        auto eol = block.find("\r\n", pos);
        if (eol == std::string_view::npos) eol = block.size();
        auto line = block.substr(pos, eol - pos);
        pos = (eol == block.size()) ? eol : eol + 2;

        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        auto name = trim(line.substr(0, colon));
        auto value = trim(line.substr(colon + 1));
        if (name.empty()) continue;
        headers_[std::string(name)] = std::string(value);
    }
}

void HttpRequest::parse_query_string(std::string_view qs) {
    std::size_t pos = 0;
    while (pos < qs.size()) {
        auto amp = qs.find('&', pos);
        if (amp == std::string_view::npos) amp = qs.size();
        auto token = qs.substr(pos, amp - pos);
        pos = (amp == qs.size()) ? amp : amp + 1;

        auto eq = token.find('=');
        if (eq == std::string_view::npos) continue;
        auto key = url_decode(token.substr(0, eq));
        auto val = url_decode(token.substr(eq + 1));
        query_params_[std::move(key)] = std::move(val);
    }
}

void HttpRequest::set_body(std::string body) {
    body_ = std::move(body);
}

std::string_view HttpRequest::header(std::string_view name) const {
    std::string key(name);
    auto it = headers_.find(key);
    if (it == headers_.end()) return {};
    return it->second;
}

bool HttpRequest::has_header(std::string_view name) const {
    std::string key(name);
    return headers_.find(key) != headers_.end();
}

std::string_view HttpRequest::query(std::string_view name) const {
    auto it = query_params_.find(std::string(name));
    if (it == query_params_.end()) return {};
    return it->second;
}

bool HttpRequest::has_query(std::string_view name) const {
    return query_params_.find(std::string(name)) != query_params_.end();
}

std::string_view HttpRequest::param(std::string_view name) const {
    auto it = path_params_.find(std::string(name));
    if (it == path_params_.end()) return {};
    return it->second;
}

bool HttpRequest::has_param(std::string_view name) const {
    return path_params_.find(std::string(name)) != path_params_.end();
}

void HttpRequest::set_param(std::string name, std::string value) {
    path_params_[std::move(name)] = std::move(value);
}

void HttpRequest::clear_params() {
    path_params_.clear();
}

std::size_t HttpRequest::content_length() const {
    auto v = header("Content-Length");
    if (v.empty()) return 0;
    std::size_t n = 0;
    auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    (void)p;
    if (ec != std::errc{}) return 0;
    return n;
}

}  // namespace fox::http
