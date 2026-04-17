#include "httpserver/http_router.h"

#include "httpserver/http_response.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace httpserver {

std::size_t HttpRouter::literal_prefix_length(const std::vector<Segment>& segs) {
    std::size_t n = 0;
    for (const auto& s : segs) {
        if (s.kind == Segment::Kind::Literal) ++n;
        else break;
    }
    return n;
}

namespace {

void default_404(HttpRequest&, HttpResponse& resp) {
    resp.set_status(404);
    resp.headers().content_type("text/plain; charset=utf-8");
    resp.set_body("404 Not Found\n");
}

}  // namespace

HttpRouter::HttpRouter()
    : not_found_handler_(default_404),
      method_not_allowed_handler_(nullptr) {}

std::vector<HttpRouter::Segment> HttpRouter::parse_pattern(std::string_view pattern) {
    std::vector<Segment> segs;
    std::size_t i = 0;
    if (i < pattern.size() && pattern[i] == '/') ++i;
    while (i < pattern.size()) {
        auto slash = pattern.find('/', i);
        auto part = pattern.substr(i, slash == std::string_view::npos ? pattern.size() - i : slash - i);
        i = (slash == std::string_view::npos) ? pattern.size() : slash + 1;
        if (part.empty()) continue;

        Segment s;
        if (part.front() == ':') {
            s.kind = Segment::Kind::Param;
            s.text = std::string(part.substr(1));
        } else if (part.front() == '*') {
            s.kind = Segment::Kind::CatchAll;
            s.text = std::string(part.substr(1));
        } else {
            s.kind = Segment::Kind::Literal;
            s.text = std::string(part);
        }
        segs.push_back(std::move(s));
    }
    return segs;
}

std::vector<std::string_view> HttpRouter::split_path(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    if (i < path.size() && path[i] == '/') ++i;
    while (i < path.size()) {
        auto slash = path.find('/', i);
        auto part = path.substr(i, slash == std::string_view::npos ? path.size() - i : slash - i);
        i = (slash == std::string_view::npos) ? path.size() : slash + 1;
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

void HttpRouter::add(HttpRequest::Method method, std::string_view pattern, HandlerFn fn) {
    auto segs = parse_pattern(pattern);

    bool has_capture = false;
    bool has_catchall = false;
    for (std::size_t k = 0; k < segs.size(); ++k) {
        if (segs[k].kind == Segment::Kind::Param) has_capture = true;
        if (segs[k].kind == Segment::Kind::CatchAll) {
            has_catchall = true;
            if (k + 1 != segs.size()) {
                // CatchAll must be the last segment. Skip registering.
                return;
            }
            has_capture = true;
        }
    }

    int method_key = static_cast<int>(method);

    if (!has_capture) {
        // Rebuild canonical path to use as key: "/a/b".
        std::string canon;
        for (const auto& s : segs) {
            canon += '/';
            canon += s.text;
        }
        if (canon.empty()) canon = "/";
        static_routes_[method_key][std::move(canon)] = std::move(fn);
        return;
    }

    DynamicRoute r;
    r.segments = std::move(segs);
    r.fn = std::move(fn);

    if (has_catchall) {
        auto& bucket = catchall_routes_[method_key];
        bucket.push_back(std::move(r));
        // Keep sorted by descending literal prefix length, then by total seg count.
        std::sort(bucket.begin(), bucket.end(),
            [](const DynamicRoute& a, const DynamicRoute& b) {
                auto la = literal_prefix_length(a.segments);
                auto lb = literal_prefix_length(b.segments);
                if (la != lb) return la > lb;
                return a.segments.size() > b.segments.size();
            });
    } else {
        dynamic_routes_[method_key][r.segments.size()].push_back(std::move(r));
    }
}

bool HttpRouter::match_dynamic(const DynamicRoute& r,
                               const std::vector<std::string_view>& path_segs,
                               HttpRequest& req) {
    const auto& pat = r.segments;
    bool has_catchall = !pat.empty() && pat.back().kind == Segment::Kind::CatchAll;

    if (has_catchall) {
        if (path_segs.size() < pat.size() - 1) return false;
        for (std::size_t k = 0; k + 1 < pat.size(); ++k) {
            const auto& s = pat[k];
            if (s.kind == Segment::Kind::Literal) {
                if (path_segs[k] != s.text) return false;
            }
            // Param matches any single segment; nothing to check.
        }
    } else {
        if (path_segs.size() != pat.size()) return false;
        for (std::size_t k = 0; k < pat.size(); ++k) {
            const auto& s = pat[k];
            if (s.kind == Segment::Kind::Literal && path_segs[k] != s.text) return false;
        }
    }

    // Match succeeded — populate params.
    for (std::size_t k = 0; k < pat.size(); ++k) {
        const auto& s = pat[k];
        if (s.kind == Segment::Kind::Param) {
            req.set_param(s.text, std::string(path_segs[k]));
        } else if (s.kind == Segment::Kind::CatchAll) {
            // Join remaining segments with '/'.
            std::string joined;
            for (std::size_t j = k; j < path_segs.size(); ++j) {
                if (j != k) joined += '/';
                joined.append(path_segs[j].data(), path_segs[j].size());
            }
            req.set_param(s.text, std::move(joined));
        }
    }
    return true;
}

bool HttpRouter::path_matches_any_method(std::string_view path) const {
    // Static: check every method bucket.
    std::string key(path);
    for (const auto& [_, m] : static_routes_) {
        if (m.find(key) != m.end()) return true;
    }

    auto segs = split_path(path);

    for (const auto& [_, by_count] : dynamic_routes_) {
        auto it = by_count.find(segs.size());
        if (it == by_count.end()) continue;
        for (const auto& r : it->second) {
            HttpRequest probe;  // discard
            if (match_dynamic(r, segs, probe)) return true;
        }
    }

    for (const auto& [_, routes] : catchall_routes_) {
        for (const auto& r : routes) {
            HttpRequest probe;
            if (match_dynamic(r, segs, probe)) return true;
        }
    }
    return false;
}

std::string HttpRouter::allowed_methods_for(std::string_view path) const {
    std::string out;
    auto append = [&](HttpRequest::Method m) {
        if (!out.empty()) out += ", ";
        out.append(HttpRequest::method_to_string(m));
    };

    std::string key(path);
    auto segs = split_path(path);

    for (int mi = 0; mi < static_cast<int>(HttpRequest::Method::UNKNOWN); ++mi) {
        auto m = static_cast<HttpRequest::Method>(mi);
        bool matches = false;

        if (auto it = static_routes_.find(mi); it != static_routes_.end()) {
            if (it->second.find(key) != it->second.end()) matches = true;
        }
        if (!matches) {
            if (auto it = dynamic_routes_.find(mi); it != dynamic_routes_.end()) {
                if (auto bucket = it->second.find(segs.size()); bucket != it->second.end()) {
                    for (const auto& r : bucket->second) {
                        HttpRequest probe;
                        if (match_dynamic(r, segs, probe)) { matches = true; break; }
                    }
                }
            }
        }
        if (!matches) {
            if (auto it = catchall_routes_.find(mi); it != catchall_routes_.end()) {
                for (const auto& r : it->second) {
                    HttpRequest probe;
                    if (match_dynamic(r, segs, probe)) { matches = true; break; }
                }
            }
        }
        if (matches) append(m);
    }
    return out;
}

void HttpRouter::set_not_found_handler(HandlerFn fn) {
    not_found_handler_ = std::move(fn);
}

void HttpRouter::set_method_not_allowed_handler(HandlerFn fn) {
    method_not_allowed_handler_ = std::move(fn);
}

void HttpRouter::handle(HttpRequest& req, HttpResponse& resp) {
    int method_key = static_cast<int>(req.method());
    std::string path(req.path());

    // 1. Static match for the requested method.
    if (auto it = static_routes_.find(method_key); it != static_routes_.end()) {
        if (auto fit = it->second.find(path); fit != it->second.end()) {
            fit->second(req, resp);
            return;
        }
    }

    // 2. Dynamic match.
    auto segs = split_path(path);
    if (auto it = dynamic_routes_.find(method_key); it != dynamic_routes_.end()) {
        if (auto bucket = it->second.find(segs.size()); bucket != it->second.end()) {
            for (const auto& r : bucket->second) {
                if (match_dynamic(r, segs, req)) {
                    r.fn(req, resp);
                    return;
                }
            }
        }
    }

    // 3. Catchall match.
    if (auto it = catchall_routes_.find(method_key); it != catchall_routes_.end()) {
        for (const auto& r : it->second) {
            if (match_dynamic(r, segs, req)) {
                r.fn(req, resp);
                return;
            }
        }
    }

    // 4. 405 if some other method matches.
    if (path_matches_any_method(path)) {
        if (method_not_allowed_handler_) {
            method_not_allowed_handler_(req, resp);
            return;
        }
        resp.set_status(405);
        resp.headers().insert("Allow", allowed_methods_for(path));
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body("405 Method Not Allowed\n");
        return;
    }

    // 5. 404.
    not_found_handler_(req, resp);
}

}  // namespace httpserver
