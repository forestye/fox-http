#include "fox-http/http_util.h"

#include "fox-http/http_response.h"

#include <charconv>

namespace fox::http::util {

std::string url_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int value = 0;
            auto [p, ec] = std::from_chars(s.data() + i + 1, s.data() + i + 3, value, 16);
            if (ec == std::errc{} && p == s.data() + i + 3) {
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

std::unordered_map<std::string, std::string> parse_form_urlencoded(std::string_view body) {
    std::unordered_map<std::string, std::string> out;
    std::size_t pos = 0;
    while (pos < body.size()) {
        auto amp = body.find('&', pos);
        if (amp == std::string_view::npos) amp = body.size();
        auto token = body.substr(pos, amp - pos);
        pos = (amp == body.size()) ? amp : amp + 1;

        auto eq = token.find('=');
        if (eq == std::string_view::npos) {
            if (!token.empty()) out[url_decode(token)] = "";
            continue;
        }
        out[url_decode(token.substr(0, eq))] = url_decode(token.substr(eq + 1));
    }
    return out;
}

namespace {

void write_plain_error(HttpResponse& resp, int status, std::string_view message) {
    resp.set_status(status);
    resp.headers().content_type("text/plain; charset=utf-8");
    resp.set_body(std::string(message));
}

}  // namespace

void bad_request(HttpResponse& resp, std::string_view message) {
    write_plain_error(resp, 400, message);
}

void not_found(HttpResponse& resp, std::string_view message) {
    write_plain_error(resp, 404, message);
}

void internal_error(HttpResponse& resp, std::string_view message) {
    write_plain_error(resp, 500, message);
}

}  // namespace fox::http::util
