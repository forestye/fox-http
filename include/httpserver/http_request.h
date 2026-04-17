#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace httpserver {

class HttpRequest {
public:
    enum class Method {
        GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, UNKNOWN
    };

    static std::string_view method_to_string(Method m);
    static Method method_from_string(std::string_view s);

    // Parse the header block (request line + headers + trailing \r\n\r\n).
    // Returns true on success. Does NOT read body; call append_body() separately
    // using the content_length() hint.
    bool parse_header(std::string_view header_block);

    // Append (or replace) the request body bytes.
    void set_body(std::string body);

    // ── Accessors ────────────────────────────────────────────────

    Method method() const { return method_; }
    std::string_view method_str() const;

    std::string_view path() const { return path_; }
    std::string_view version() const { return version_; }
    std::string_view body() const { return body_; }

    // Header lookup is case-insensitive (RFC 7230).
    std::string_view header(std::string_view name) const;
    bool has_header(std::string_view name) const;

    // Query parameters (URL-decoded).
    std::string_view query(std::string_view name) const;
    bool has_query(std::string_view name) const;
    const std::map<std::string, std::string>& queries() const { return query_params_; }

    // Path parameters captured by a dynamic route (e.g. ":id" or "*path").
    // Set by HttpRouter before invoking the registered handler.
    std::string_view param(std::string_view name) const;
    bool has_param(std::string_view name) const;
    const std::map<std::string, std::string>& params() const { return path_params_; }
    void set_param(std::string name, std::string value);
    void clear_params();

    // Returns Content-Length header value, or 0 if absent/invalid.
    std::size_t content_length() const;

private:
    struct CiLess {
        bool operator()(const std::string& a, const std::string& b) const noexcept;
    };

    void parse_request_line(std::string_view line);
    void parse_headers(std::string_view block);
    void parse_query_string(std::string_view qs);

    Method method_ = Method::UNKNOWN;
    std::string path_;
    std::string version_;
    std::map<std::string, std::string, CiLess> headers_;
    std::string body_;
    std::map<std::string, std::string> query_params_;
    std::map<std::string, std::string> path_params_;
};

}  // namespace httpserver
