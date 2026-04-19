#include "fox-http/http_response.h"

#include "response_stream.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <string>

namespace fox::http {

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
    assert(mode_ == Mode::Buffered && "set_body after another mode was chosen");
    body_ = std::move(body);
}

void HttpResponse::stream(StreamFn fn) {
    assert(mode_ == Mode::Buffered && "stream() called after another mode was chosen");
    assert(body_.empty() && "stream() called after set_body() — modes are exclusive");
    assert(!headers_flushed_ && "stream() called after headers were already flushed");
    mode_ = Mode::Stream;
    stream_fn_ = std::move(fn);
}

std::string HttpResponse::build_header_block() const {
    std::string out;
    out.reserve(128);
    out.append("HTTP/1.1 ");
    out.append(std::to_string(status_));
    out.append(" ");
    out.append(reason_phrase(status_));
    out.append("\r\n");
    for (const auto& e : headers_.entries_) {
        out.append(e.name);
        out.append(": ");
        out.append(e.value);
        out.append("\r\n");
    }
    out.append("\r\n");
    return out;
}

bool HttpResponse::flush_headers() {
    // Used by paths that don't carry any payload (e.g. end_chunks when no
    // chunks were written). Payload-carrying paths (write/writev/write_chunk)
    // coalesce headers into their first syscall instead.
    if (headers_flushed_) return true;
    if (!stream_) return false;
    header_block_cache_ = build_header_block();
    bool ok = stream_->write(header_block_cache_.data(), header_block_cache_.size());
    if (ok) headers_flushed_ = true;
    return ok;
}

bool HttpResponse::write(const void* data, std::size_t n) {
    assert(!(mode_ == Mode::Buffered && !body_.empty()) &&
           "write() after set_body() — modes are exclusive");
    if (mode_ == Mode::Buffered) mode_ = Mode::Immediate;
    if (!stream_) return false;

    if (!headers_flushed_) {
        // Coalesce headers + payload into one writev to avoid a Nagle/ACK
        // delay between two syscalls.
        header_block_cache_ = build_header_block();
        struct iovec iv[2];
        iv[0].iov_base = const_cast<char*>(header_block_cache_.data());
        iv[0].iov_len = header_block_cache_.size();
        iv[1].iov_base = const_cast<void*>(data);
        iv[1].iov_len = n;
        int count = (n == 0) ? 1 : 2;
        bool ok = stream_->writev(iv, count);
        if (ok) headers_flushed_ = true;
        return ok;
    }
    if (n == 0) return true;
    return stream_->write(data, n);
}

bool HttpResponse::writev(const struct iovec* iov, int iovcnt) {
    assert(!(mode_ == Mode::Buffered && !body_.empty()) &&
           "writev() after set_body() — modes are exclusive");
    if (mode_ == Mode::Buffered) mode_ = Mode::Immediate;
    if (!stream_) return false;

    if (!headers_flushed_) {
        header_block_cache_ = build_header_block();
        // Build a combined iov with headers at [0], payload segments after.
        std::vector<struct iovec> combined;
        combined.reserve(static_cast<std::size_t>(iovcnt) + 1);
        combined.push_back({const_cast<char*>(header_block_cache_.data()),
                            header_block_cache_.size()});
        for (int i = 0; i < iovcnt; ++i) combined.push_back(iov[i]);
        bool ok = stream_->writev(combined.data(), static_cast<int>(combined.size()));
        if (ok) headers_flushed_ = true;
        return ok;
    }
    if (iovcnt <= 0) return true;
    return stream_->writev(iov, iovcnt);
}

bool HttpResponse::write_chunk(const void* data, std::size_t n) {
    // Empty chunk is reserved as the terminator in HTTP chunked encoding;
    // route those through end_chunks() and silently skip here.
    if (n == 0) return true;

    if (!headers_flushed_ && !chunked_) {
        if (!headers_.contains("Transfer-Encoding")) {
            headers_.insert("Transfer-Encoding", "chunked");
        }
        chunked_ = true;
    }
    if (mode_ == Mode::Buffered) mode_ = Mode::Immediate;
    if (!stream_) return false;

    char size_line[32];
    int len = std::snprintf(size_line, sizeof(size_line), "%zx\r\n", n);
    if (len <= 0) return false;
    static const char crlf[2] = {'\r', '\n'};

    if (!headers_flushed_) {
        header_block_cache_ = build_header_block();
        struct iovec iv[4];
        iv[0].iov_base = const_cast<char*>(header_block_cache_.data());
        iv[0].iov_len = header_block_cache_.size();
        iv[1].iov_base = size_line;
        iv[1].iov_len = static_cast<std::size_t>(len);
        iv[2].iov_base = const_cast<void*>(data);
        iv[2].iov_len = n;
        iv[3].iov_base = const_cast<char*>(crlf);
        iv[3].iov_len = 2;
        bool ok = stream_->writev(iv, 4);
        if (ok) headers_flushed_ = true;
        return ok;
    }

    struct iovec iv[3];
    iv[0].iov_base = size_line;
    iv[0].iov_len = static_cast<std::size_t>(len);
    iv[1].iov_base = const_cast<void*>(data);
    iv[1].iov_len = n;
    iv[2].iov_base = const_cast<char*>(crlf);
    iv[2].iov_len = 2;
    return stream_->writev(iv, 3);
}

bool HttpResponse::end_chunks() {
    if (!chunked_) return true;  // not chunked; nothing to terminate
    if (!stream_) return false;
    if (!flush_headers()) return false;
    static const char terminator[] = "0\r\n\r\n";
    return stream_->write(terminator, sizeof(terminator) - 1);
}

bool HttpResponse::write_sse_event(std::string_view event, std::string_view data) {
    // SSE frame: "event: <name>\ndata: <data>\n\n"
    // If event is empty, omit the event line (unnamed event).
    std::string frame;
    frame.reserve(32 + event.size() + data.size());
    if (!event.empty()) {
        frame.append("event: ");
        frame.append(event);
        frame.push_back('\n');
    }
    // For multi-line data, each line must be prefixed with "data: ".
    std::size_t pos = 0;
    while (pos < data.size()) {
        auto nl = data.find('\n', pos);
        frame.append("data: ");
        if (nl == std::string_view::npos) {
            frame.append(data.substr(pos));
            pos = data.size();
        } else {
            frame.append(data.substr(pos, nl - pos));
            pos = nl + 1;
        }
        frame.push_back('\n');
    }
    frame.push_back('\n');
    return write(frame.data(), frame.size());
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
    assert(mode_ == Mode::Buffered && "serialize() called on non-Buffered response");
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

}  // namespace fox::http
