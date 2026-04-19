#include "fox-http/http_request.h"

#include <gtest/gtest.h>

using namespace fox::http;

TEST(Request, BasicGet) {
    HttpRequest r;
    ASSERT_TRUE(r.parse_header("GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(r.method(), HttpRequest::Method::GET);
    EXPECT_EQ(r.path(), "/hello");
    EXPECT_EQ(r.version(), "HTTP/1.1");
    EXPECT_EQ(r.header("Host"), "x");
}

TEST(Request, CaseInsensitiveHeader) {
    HttpRequest r;
    ASSERT_TRUE(r.parse_header("GET / HTTP/1.1\r\nContent-Length: 42\r\n\r\n"));
    EXPECT_EQ(r.header("content-length"), "42");
    EXPECT_EQ(r.header("CONTENT-LENGTH"), "42");
    EXPECT_TRUE(r.has_header("Content-Length"));
    EXPECT_EQ(r.content_length(), 42u);
}

TEST(Request, QueryParsing) {
    HttpRequest r;
    ASSERT_TRUE(r.parse_header("GET /x?a=1&b=hello%20world HTTP/1.1\r\n\r\n"));
    EXPECT_EQ(r.path(), "/x");
    EXPECT_EQ(r.query("a"), "1");
    EXPECT_EQ(r.query("b"), "hello world");
    EXPECT_TRUE(r.has_query("a"));
    EXPECT_FALSE(r.has_query("c"));
}

TEST(Request, MethodParsing) {
    struct C { const char* name; HttpRequest::Method expected; };
    for (auto& c : {
        C{"GET", HttpRequest::Method::GET},
        C{"POST", HttpRequest::Method::POST},
        C{"PUT", HttpRequest::Method::PUT},
        C{"DELETE", HttpRequest::Method::DELETE},
        C{"PATCH", HttpRequest::Method::PATCH},
        C{"HEAD", HttpRequest::Method::HEAD},
        C{"OPTIONS", HttpRequest::Method::OPTIONS},
    }) {
        HttpRequest r;
        std::string h = std::string(c.name) + " / HTTP/1.1\r\n\r\n";
        ASSERT_TRUE(r.parse_header(h));
        EXPECT_EQ(r.method(), c.expected) << c.name;
    }
}

TEST(Request, MalformedHeader) {
    HttpRequest r;
    EXPECT_FALSE(r.parse_header("not a request line"));
}

TEST(Request, Params) {
    HttpRequest r;
    r.set_param("id", "42");
    EXPECT_EQ(r.param("id"), "42");
    EXPECT_TRUE(r.has_param("id"));
    EXPECT_FALSE(r.has_param("missing"));
    r.clear_params();
    EXPECT_FALSE(r.has_param("id"));
}
