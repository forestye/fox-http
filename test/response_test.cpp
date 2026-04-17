#include "httpserver/http_response.h"
#include "../src/response_stream.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace httpserver;

namespace {

// Fake ResponseStream that accumulates bytes into an internal buffer.
class FakeStream : public ResponseStream {
public:
    bool write(const void* data, std::size_t n) override {
        buf_.append(static_cast<const char*>(data), n);
        ++write_calls_;
        return true;
    }
    bool writev(const struct iovec* iov, int iovcnt) override {
        for (int i = 0; i < iovcnt; ++i) {
            buf_.append(static_cast<const char*>(iov[i].iov_base), iov[i].iov_len);
        }
        ++writev_calls_;
        return true;
    }

    std::string buf_;
    int write_calls_ = 0;
    int writev_calls_ = 0;
};

}  // namespace

TEST(Response, BufferedSerializesAutoContentLength) {
    HttpResponse r;
    r.set_status(200);
    r.headers().content_type("text/plain");
    r.set_body("hello");
    auto s = r.serialize();
    EXPECT_NE(s.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(s.find("Content-Type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(s.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_EQ(s.substr(s.size() - 5), "hello");
}

TEST(Response, ImmediateWriteCoalescesHeadersAndPayload) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    r.headers().content_length(5);
    r.write("hello", 5);

    // One coalesced writev, not a separate write for headers.
    EXPECT_EQ(fs.write_calls_, 0);
    EXPECT_EQ(fs.writev_calls_, 1);
    EXPECT_NE(fs.buf_.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(fs.buf_.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_EQ(fs.buf_.substr(fs.buf_.size() - 5), "hello");
}

TEST(Response, ImmediateWritevZeroCopyIov) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    const char part1[] = "<html>";
    const char part2[] = "</html>";
    r.headers().content_length(sizeof(part1) - 1 + sizeof(part2) - 1);

    struct iovec iov[2];
    iov[0].iov_base = const_cast<char*>(part1); iov[0].iov_len = sizeof(part1) - 1;
    iov[1].iov_base = const_cast<char*>(part2); iov[1].iov_len = sizeof(part2) - 1;
    r.writev(iov, 2);

    EXPECT_EQ(fs.writev_calls_, 1);
    EXPECT_NE(fs.buf_.find("<html></html>"), std::string::npos);
}

TEST(Response, ImmediateSubsequentWritesSkipHeaders) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    r.headers().content_length(10);
    r.write("hello", 5);
    r.write("world", 5);

    // First call: writev (headers+data). Second: plain write.
    EXPECT_EQ(fs.writev_calls_, 1);
    EXPECT_EQ(fs.write_calls_, 1);
    EXPECT_EQ(fs.buf_.substr(fs.buf_.size() - 10), "helloworld");
}

TEST(Response, WriteChunkAddsTransferEncoding) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    r.headers().content_type("text/plain");
    r.write_chunk("hello", 5);
    r.end_chunks();

    EXPECT_NE(fs.buf_.find("Transfer-Encoding: chunked\r\n"), std::string::npos);
    EXPECT_NE(fs.buf_.find("5\r\nhello\r\n"), std::string::npos);
    EXPECT_NE(fs.buf_.find("0\r\n\r\n"), std::string::npos);
}

TEST(Response, StreamMode) {
    HttpResponse r;
    bool ran = false;
    r.stream([&](HttpResponse&) { ran = true; });
    EXPECT_EQ(r.mode(), HttpResponse::Mode::Stream);
    EXPECT_FALSE(ran);  // Not invoked until dispatcher posts it.
    // Simulate dispatcher invocation:
    if (r.stream_fn()) r.stream_fn()(r);
    EXPECT_TRUE(ran);
}

TEST(Response, SseFormatsFrame) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    r.headers().content_type("text/event-stream");
    r.write_sse_event("message", "hello");
    // Accept either plain write or coalesced writev path.
    auto idx = fs.buf_.rfind("\r\n\r\n");
    ASSERT_NE(idx, std::string::npos);
    auto frame = fs.buf_.substr(idx + 4);
    EXPECT_EQ(frame, "event: message\ndata: hello\n\n");
}

TEST(Response, SseMultilineData) {
    HttpResponse r;
    FakeStream fs;
    r.attach_stream(&fs);
    r.write_sse_event("", "line1\nline2");
    auto idx = fs.buf_.rfind("\r\n\r\n");
    ASSERT_NE(idx, std::string::npos);
    auto frame = fs.buf_.substr(idx + 4);
    EXPECT_EQ(frame, "data: line1\ndata: line2\n\n");
}
