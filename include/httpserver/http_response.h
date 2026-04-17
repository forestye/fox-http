#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace httpserver {

class HttpResponse {
public:
    class Headers {
    public:
        // Chainable insert. Overwrites an existing same-name entry.
        Headers& insert(std::string_view name, std::string_view value);

        Headers& content_length(std::size_t n);
        Headers& content_type(std::string_view ct);

        std::string_view get(std::string_view name) const;
        bool contains(std::string_view name) const;

    private:
        friend class HttpResponse;

        struct Entry {
            std::string name;
            std::string value;
        };

        std::vector<Entry>::iterator find_ci(std::string_view name);
        std::vector<Entry>::const_iterator find_ci(std::string_view name) const;

        std::vector<Entry> entries_;
    };

    HttpResponse() = default;

    // ── Status ───────────────────────────────────────────────────

    void set_status(int code) { status_ = code; }
    int status() const { return status_; }

    // ── Headers ──────────────────────────────────────────────────

    Headers& headers() { return headers_; }
    const Headers& headers() const { return headers_; }

    // ── Body (Buffered mode) ─────────────────────────────────────

    // Sets the full response body. Content-Length is set automatically
    // during serialization if not already specified.
    void set_body(std::string body);

    std::string_view body() const { return body_; }

    // Serialize status line + headers + body into a single HTTP/1.1 response.
    // Used by Connection; not typically called by user code.
    std::string serialize() const;

    // Map a status code to its standard reason phrase (e.g. 200 -> "OK").
    static std::string_view reason_phrase(int code);

private:
    int status_ = 200;
    Headers headers_;
    std::string body_;
};

}  // namespace httpserver
