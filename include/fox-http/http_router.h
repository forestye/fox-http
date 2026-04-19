#pragma once

#include "fox-http/http_handler.h"
#include "fox-http/http_request.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fox::http {

class HttpResponse;

// Built-in router that itself is an HttpHandler. Supports:
//   - static routes:    "/hello"
//   - param segments:   "/user/:id"         → req.param("id")
//   - catchall suffix:  "/files/*path"      → req.param("path")
//
// Matching order (per request):
//   1. Exact static match (method+path)
//   2. Dynamic match with identical segment count
//   3. Catchall match (longest literal prefix wins)
//   4. If any other method matches the same path → 405 Method Not Allowed
//   5. Otherwise 404 Not Found
class HttpRouter : public HttpHandler {
public:
    using HandlerFn = std::function<void(HttpRequest&, HttpResponse&)>;

    HttpRouter();

    // Register a route. `pattern` uses '/' as segment separator.
    // A segment beginning with ':' captures one path segment.
    // A segment beginning with '*' (must be last) captures the remaining suffix.
    void add(HttpRequest::Method method, std::string_view pattern, HandlerFn fn);

    // Convenience aliases.
    void get(std::string_view p, HandlerFn fn)     { add(HttpRequest::Method::GET,     p, std::move(fn)); }
    void post(std::string_view p, HandlerFn fn)    { add(HttpRequest::Method::POST,    p, std::move(fn)); }
    void put(std::string_view p, HandlerFn fn)     { add(HttpRequest::Method::PUT,     p, std::move(fn)); }
    void del(std::string_view p, HandlerFn fn)     { add(HttpRequest::Method::DELETE,  p, std::move(fn)); }
    void patch(std::string_view p, HandlerFn fn)   { add(HttpRequest::Method::PATCH,   p, std::move(fn)); }
    void head(std::string_view p, HandlerFn fn)    { add(HttpRequest::Method::HEAD,    p, std::move(fn)); }
    void options(std::string_view p, HandlerFn fn) { add(HttpRequest::Method::OPTIONS, p, std::move(fn)); }

    // Override the default 404 / 405 responses.
    void set_not_found_handler(HandlerFn fn);
    void set_method_not_allowed_handler(HandlerFn fn);

    void handle(HttpRequest& req, HttpResponse& resp) override;

private:
    struct Segment {
        enum class Kind { Literal, Param, CatchAll };
        Kind kind = Kind::Literal;
        std::string text;  // literal, or capture name
    };

    struct DynamicRoute {
        std::vector<Segment> segments;  // last may be CatchAll
        HandlerFn fn;
    };

    // Static: method+path → handler
    using StaticMap = std::unordered_map<std::string, HandlerFn>;
    std::unordered_map<int, StaticMap> static_routes_;  // key is Method int

    // Dynamic non-catchall routes bucketed by segment count.
    // method → seg_count → [routes]
    std::unordered_map<int, std::unordered_map<std::size_t, std::vector<DynamicRoute>>> dynamic_routes_;

    // Catchall routes: method → [routes], kept sorted by descending literal prefix length.
    std::unordered_map<int, std::vector<DynamicRoute>> catchall_routes_;

    HandlerFn not_found_handler_;
    HandlerFn method_not_allowed_handler_;

    // Parse a pattern like "/user/:id/posts/*rest" into segments.
    static std::vector<Segment> parse_pattern(std::string_view pattern);

    // Split a concrete path into segment views (no leading slash, no empty).
    static std::vector<std::string_view> split_path(std::string_view path);

    // Number of leading Literal segments (for ordering catchall routes).
    static std::size_t literal_prefix_length(const std::vector<Segment>& segs);

    // Try to match a single dynamic route against the path segments.
    // On success, populates req's params and returns true.
    static bool match_dynamic(const DynamicRoute& r,
                              const std::vector<std::string_view>& path_segs,
                              HttpRequest& req);

    // Check if any method has a matching route for `path` (for 405 detection).
    bool path_matches_any_method(std::string_view path) const;

    // Collect methods that match `path` (for Allow header).
    std::string allowed_methods_for(std::string_view path) const;
};

}  // namespace fox::http
