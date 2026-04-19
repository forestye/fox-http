#include "server.h"
#include "routing_module.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
using namespace std;


class ServerTest : public ::testing::Test {
protected:
    RoutingModule routing_module;
    unsigned short test_port = 8080;

    void SetUp() override {
        // Add your setup code here, if needed
    }

    void TearDown() override {
        // Add your cleanup code here, if needed
    }
};

void send_test_request(int port) {
    using tcp = boost::asio::ip::tcp;
    namespace http = boost::beast::http;

    // I/O context
    boost::asio::io_context ioc;

    // Resolver and socket
    tcp::resolver resolver(ioc);
    tcp::socket socket(ioc);
    // Resolve localhost and connect
    auto const results = resolver.resolve("localhost", std::to_string(port));

    boost::system::error_code ec;
    boost::asio::connect(socket, results.begin(), results.end(), ec);
    if (ec) {
        std::cerr << "Error connecting: " << ec.message() << std::endl;
        return;
    }


    // Prepare request
    http::request<http::string_body> req(http::verb::get, "/test", 11);
    req.set(http::field::host, "localhost");
    req.set(http::field::user_agent, "CustomUserAgent/1.0");
    // Send request
    http::write(socket, req);
    // Receive response
    boost::beast::flat_buffer buffer;
    //http::response<http::dynamic_body> res;
    http::response<http::string_body> res;

    http::read(socket, buffer, res, ec);
    if (ec) {
        std::cerr << "Error reading response: " << ec.message() << std::endl;
        return;
    }

    // Gracefully close the socket
    socket.shutdown(tcp::socket::shutdown_both, ec);
    // Check if any error occurred
    if (ec && ec != boost::asio::error::eof) {
        throw boost::system::system_error{ec};
    }
}


TEST_F(ServerTest, StartAndStopServer) {
    auto server = std::make_shared<Server>(test_port, routing_module);
    // Run the server asynchronously
    auto server_future = std::async(std::launch::async, [&server]() { server->run(); });
    // Sleep for a short time to ensure the server has started
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Send a request to the server
    // Run send_test_request asynchronously
    auto request_future = std::async(std::launch::async, [&]() { send_test_request(test_port); });
    // Check if any exception occurred during the request
    try {
        request_future.get();
    } catch (const std::exception& e) {
        std::cerr << "Request failed with exception: " << e.what() << std::endl;
    }

    // Stop the server
    server->stop();
    // Wait for the server to finish
    server_future.wait();
}

