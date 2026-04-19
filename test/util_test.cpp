#include "fox-http/http_response.h"
#include "fox-http/http_util.h"

#include <gtest/gtest.h>

using namespace fox::http;
using fox::http::util::parse_form_urlencoded;
using fox::http::util::stringify;
using fox::http::util::url_decode;

TEST(UrlDecode, PassThrough) {
    EXPECT_EQ(url_decode("hello"), "hello");
    EXPECT_EQ(url_decode(""), "");
}

TEST(UrlDecode, PercentHex) {
    EXPECT_EQ(url_decode("New%20York"), "New York");
    EXPECT_EQ(url_decode("%3F%3D%26"), "?=&");
}

TEST(UrlDecode, PlusIsSpace) {
    EXPECT_EQ(url_decode("a+b"), "a b");
}

TEST(UrlDecode, MalformedPercentPassesThrough) {
    EXPECT_EQ(url_decode("%"), "%");
    EXPECT_EQ(url_decode("%Z0"), "%Z0");
}

TEST(ParseForm, Simple) {
    auto m = parse_form_urlencoded("name=John&age=30");
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m["name"], "John");
    EXPECT_EQ(m["age"], "30");
}

TEST(ParseForm, DecodedValues) {
    auto m = parse_form_urlencoded("city=New%20York&q=a+b");
    EXPECT_EQ(m["city"], "New York");
    EXPECT_EQ(m["q"], "a b");
}

TEST(ParseForm, EmptyValue) {
    auto m = parse_form_urlencoded("k=&j");
    EXPECT_EQ(m["k"], "");
    EXPECT_EQ(m["j"], "");
}

TEST(ParseForm, Empty) {
    auto m = parse_form_urlencoded("");
    EXPECT_TRUE(m.empty());
}

TEST(Stringify, Strings) {
    EXPECT_EQ(stringify(std::string("hi")), "hi");
    EXPECT_EQ(stringify("hi"), "hi");
    EXPECT_EQ(stringify(std::string_view("hi")), "hi");
}

TEST(Stringify, Numbers) {
    EXPECT_EQ(stringify(42), "42");
    EXPECT_EQ(stringify(3.5), "3.500000");
}

TEST(ErrorHelpers, BadRequest) {
    HttpResponse r;
    util::bad_request(r, "oops");
    EXPECT_EQ(r.status(), 400);
    EXPECT_EQ(r.body(), "oops");
    EXPECT_EQ(r.headers().get("Content-Type"), "text/plain; charset=utf-8");
}
