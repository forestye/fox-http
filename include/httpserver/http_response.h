#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <sys/uio.h>  // iovec
#include <vector>

namespace httpserver {

class ResponseStream;  // internal interface defined in src/response_stream.h

class HttpResponse {
public:
    // A response is written to the wire via exactly one of these modes. The
    // first mutating call (set_body / write / writev / stream) locks the mode.
    //
    //   Buffered   — default. Handler fills body, Connection serializes and
    //                async_writes after handler returns.
    //   Immediate  — writev / write sends bytes synchronously during the
    //                handler call. Status line + headers are flushed on the
    //                first write. Preserves zero-copy for scatter-gather I/O.
    //   Stream     — handler calls stream(fn); fn is dispatched to the handler
    //                thread pool after handler returns. Inside fn, write_chunk
    //                / writev / write_sse_event send bytes synchronously.
    enum class Mode {
        Buffered,
        Immediate,
        Stream,
    };

    class Headers {
    public:
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

    // ── Buffered mode ────────────────────────────────────────────
    // Sets the full response body. Content-Length is auto-set during
    // serialization if not already specified.
    void set_body(std::string body);
    std::string_view body() const { return body_; }

    // ── Immediate mode (zero-copy, sync on io thread) ────────────
    // First call flushes "HTTP/1.1 <status> <reason>\r\n<headers>\r\n\r\n"
    // followed by the payload. Subsequent calls send raw payload bytes.
    // Handler MUST set headers().content_length(n) (or
    // Transfer-Encoding: chunked) before the first write, otherwise
    // keep-alive is disabled for this response.
    bool write(const void* data, std::size_t n);
    bool writev(const struct iovec* iov, int iovcnt);

    // ── Stream mode (off-thread, long-lived) ─────────────────────
    // Marks the response for post-handler execution. fn runs on the handler
    // thread pool with the same HttpResponse object as argument. Inside fn,
    // use write / writev / write_chunk / write_sse_event.
    using StreamFn = std::function<void(HttpResponse&)>;
    void stream(StreamFn fn);

    // Chunked helpers (Transfer-Encoding: chunked).
    // First call auto-inserts the Transfer-Encoding header and flushes
    // status line + headers. end_chunks() sends the terminating "0\r\n\r\n".
    bool write_chunk(const void* data, std::size_t n);
    bool write_chunk(std::string_view s) { return write_chunk(s.data(), s.size()); }
    bool end_chunks();

    // Server-Sent Events helper. event may be empty for unnamed events.
    // Formats "event: <name>\ndata: <data>\n\n"; requires Content-Type to be
    // set to "text/event-stream" by the handler (not enforced here).
    bool write_sse_event(std::string_view event, std::string_view data);

    // ── Internal API used by Connection ──────────────────────────
    Mode mode() const { return mode_; }
    bool headers_flushed() const { return headers_flushed_; }
    StreamFn& stream_fn() { return stream_fn_; }

    // Attach the wire stream. Connection calls this once per request before
    // invoking the handler. Not part of the public handler API.
    void attach_stream(ResponseStream* s) { stream_ = s; }

    // Serialize status line + headers + body into a single HTTP/1.1 response
    // (Buffered mode only). Used by Connection.
    std::string serialize() const;

    // Map a status code to its standard reason phrase (e.g. 200 -> "OK").
    static std::string_view reason_phrase(int code);

private:
    // Flush status line + headers synchronously via stream_. Sets
    // headers_flushed_. Called automatically by the first write/writev in
    // Immediate mode or by write_chunk.
    bool flush_headers();

    // Build the status line + headers block as a string.
    std::string build_header_block() const;

    int status_ = 200;
    Headers headers_;
    std::string body_;

    Mode mode_ = Mode::Buffered;
    bool headers_flushed_ = false;
    bool chunked_ = false;

    ResponseStream* stream_ = nullptr;
    StreamFn stream_fn_;

    // Kept alive across the iov lifetime of the first coalesced write.
    std::string header_block_cache_;
};

}  // namespace httpserver
