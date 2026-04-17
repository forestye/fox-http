#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace httpserver {

class HttpResponse;

namespace util {

// URL-decode a percent-encoded string. '+' is translated to ' ' (form-style).
std::string url_decode(std::string_view s);

// Parse an application/x-www-form-urlencoded body.
// Example: "name=John&age=30&city=New%20York"
//   → {{"name","John"}, {"age","30"}, {"city","New York"}}
std::unordered_map<std::string, std::string> parse_form_urlencoded(std::string_view body);

// Stringify a value: strings pass through, numbers go through std::to_string.
template <typename T>
inline std::string stringify(T&& v) {
    using D = typename std::decay<T>::type;
    if constexpr (std::is_same_v<D, std::string> ||
                  std::is_same_v<D, const char*> ||
                  std::is_same_v<D, char*> ||
                  std::is_same_v<D, std::string_view>) {
        return std::string(std::forward<T>(v));
    } else {
        return std::to_string(std::forward<T>(v));
    }
}

// Convenience error responses (Buffered mode).
void bad_request(HttpResponse& resp, std::string_view message = "bad request");
void not_found(HttpResponse& resp, std::string_view message = "not found");
void internal_error(HttpResponse& resp, std::string_view message = "internal server error");

}  // namespace util
}  // namespace httpserver
