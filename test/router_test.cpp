#include "httpserver/http_request.h"
#include "httpserver/http_response.h"
#include "httpserver/http_router.h"

#include <gtest/gtest.h>

using namespace httpserver;

namespace {

HttpRequest make_request(HttpRequest::Method method, std::string_view path) {
    std::string line;
    line.append(HttpRequest::method_to_string(method));
    line += ' ';
    line.append(path);
    line += " HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    req.parse_header(line);
    return req;
}

}  // namespace

TEST(Router, StaticRouteHit) {
    HttpRouter r;
    r.get("/hello", [](HttpRequest&, HttpResponse& resp) {
        resp.set_body("ok");
    });

    auto req = make_request(HttpRequest::Method::GET, "/hello");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.status(), 200);
    EXPECT_EQ(resp.body(), "ok");
}

TEST(Router, NotFoundDefault) {
    HttpRouter r;
    r.get("/hello", [](HttpRequest&, HttpResponse&) {});
    auto req = make_request(HttpRequest::Method::GET, "/nope");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.status(), 404);
}

TEST(Router, MethodNotAllowed) {
    HttpRouter r;
    r.get("/hello", [](HttpRequest&, HttpResponse&) {});
    auto req = make_request(HttpRequest::Method::POST, "/hello");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.status(), 405);
    EXPECT_EQ(resp.headers().get("Allow"), "GET");
}

TEST(Router, DynamicParam) {
    HttpRouter r;
    r.get("/user/:id", [](HttpRequest& req, HttpResponse& resp) {
        resp.set_body(std::string(req.param("id")));
    });

    auto req = make_request(HttpRequest::Method::GET, "/user/42");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.status(), 200);
    EXPECT_EQ(resp.body(), "42");
}

TEST(Router, MultipleParams) {
    HttpRouter r;
    r.get("/user/:uid/post/:pid", [](HttpRequest& req, HttpResponse& resp) {
        std::string out;
        out.append(req.param("uid"));
        out += '-';
        out.append(req.param("pid"));
        resp.set_body(out);
    });

    auto req = make_request(HttpRequest::Method::GET, "/user/7/post/99");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.body(), "7-99");
}

TEST(Router, CatchAll) {
    HttpRouter r;
    r.get("/files/*rest", [](HttpRequest& req, HttpResponse& resp) {
        resp.set_body(std::string(req.param("rest")));
    });

    auto req = make_request(HttpRequest::Method::GET, "/files/a/b/c.txt");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.body(), "a/b/c.txt");
}

TEST(Router, StaticTakesPriorityOverDynamic) {
    HttpRouter r;
    r.get("/user/:id", [](HttpRequest&, HttpResponse& resp) { resp.set_body("dyn"); });
    r.get("/user/me", [](HttpRequest&, HttpResponse& resp) { resp.set_body("static"); });

    HttpResponse resp1;
    auto req1 = make_request(HttpRequest::Method::GET, "/user/me");
    r.handle(req1, resp1);
    EXPECT_EQ(resp1.body(), "static");

    HttpResponse resp2;
    auto req2 = make_request(HttpRequest::Method::GET, "/user/42");
    r.handle(req2, resp2);
    EXPECT_EQ(resp2.body(), "dyn");
}

TEST(Router, CatchAllLongerPrefixWins) {
    HttpRouter r;
    r.get("/*rest", [](HttpRequest& req, HttpResponse& resp) {
        resp.set_body("root:" + std::string(req.param("rest")));
    });
    r.get("/api/*rest", [](HttpRequest& req, HttpResponse& resp) {
        resp.set_body("api:" + std::string(req.param("rest")));
    });

    HttpResponse resp;
    auto req = make_request(HttpRequest::Method::GET, "/api/v1/users");
    r.handle(req, resp);
    EXPECT_EQ(resp.body(), "api:v1/users");
}

TEST(Router, CustomNotFound) {
    HttpRouter r;
    r.get("/hello", [](HttpRequest&, HttpResponse&) {});
    r.set_not_found_handler([](HttpRequest&, HttpResponse& resp) {
        resp.set_status(418);
        resp.set_body("teapot");
    });

    auto req = make_request(HttpRequest::Method::GET, "/nope");
    HttpResponse resp;
    r.handle(req, resp);
    EXPECT_EQ(resp.status(), 418);
}

TEST(Router, MultipleMethodsSamePath) {
    HttpRouter r;
    r.get("/x", [](HttpRequest&, HttpResponse& resp) { resp.set_body("g"); });
    r.post("/x", [](HttpRequest&, HttpResponse& resp) { resp.set_body("p"); });

    HttpResponse rg;
    auto reqg = make_request(HttpRequest::Method::GET, "/x");
    r.handle(reqg, rg);
    EXPECT_EQ(rg.body(), "g");

    HttpResponse rp;
    auto reqp = make_request(HttpRequest::Method::POST, "/x");
    r.handle(reqp, rp);
    EXPECT_EQ(rp.body(), "p");

    HttpResponse rd;
    auto reqd = make_request(HttpRequest::Method::DELETE, "/x");
    r.handle(reqd, rd);
    EXPECT_EQ(rd.status(), 405);
    auto allow = rd.headers().get("Allow");
    // Order is GET, POST based on enum order.
    EXPECT_NE(allow.find("GET"), std::string_view::npos);
    EXPECT_NE(allow.find("POST"), std::string_view::npos);
}
