#include <gtest/gtest.h>
#include "routing_module.h"
#include "request_handler.h"

class TestRequestHandler : public BaseRequestHandler {
public:
    void operator()(const HttpRequest &request, HttpResponse &response) override {
        set_common_response_headers(response);
        response.set_body("Test Handler");
    }
};

class RoutingModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_handler = std::make_shared<TestRequestHandler>();
        routing_module.register_handler("/test", HttpRequest::Method::GET, test_handler);
    }

    RoutingModule routing_module;
    std::shared_ptr<TestRequestHandler> test_handler;
};

TEST_F(RoutingModuleTest, HandlerIsRegistered) {
    std::stringstream request_stream("GET /test HTTP/1.1\r\n\r\n");
    HttpRequest request;
    request.parse(request_stream);
    HttpResponse response;

    routing_module.route_request(request, response);
    EXPECT_EQ(response.to_string(), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 12\r\n\r\nTest Handler");
}

TEST_F(RoutingModuleTest, HandlerNotFound) {
    std::stringstream request_stream("PUT /test HTTP/1.1\r\n\r\n");
    HttpRequest request;
    request.parse(request_stream);
    HttpResponse response;

    routing_module.route_request(request, response);
    EXPECT_EQ(response.to_string(), "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
