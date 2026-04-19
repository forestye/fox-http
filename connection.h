/*
Defines Connection class representing a connection with a client.
Connection class manages connection lifecycle, request reading, response writing, and request handling via RoutingModule.
Uses boost::asio library for network communication and std::shared_ptr for Connection object lifecycle management.
*/
#ifndef CONNECTION_H
#define CONNECTION_H

#include <boost/asio.hpp>
#include <memory>
#include <chrono>
#include "http_request.h"
#include "http_response.h"
#include "routing_module.h"

namespace asio = boost::asio;
using asio::ip::tcp;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    explicit Connection(asio::io_context& io_context, RoutingModule& routing_module);

    ~Connection();

    // Get the socket associated with the connection
    tcp::socket& socket();

    // Start processing the connection
    void start();

    void cancel();

	void close();

    // Check if the connection is idle
    bool is_idle() const;

    // Get the unique ID of the connection
    std::size_t get_id() const { return reinterpret_cast<std::size_t>(this); }

private:
    // Read data from the socket
    void read();

    // Write data to the socket
    void write(const std::string& data);

    // Handle the request and generate a response
    void handle_request(const HttpRequest& request, HttpResponse& response);

    // Socket for the connection
    tcp::socket socket_;

    // Buffer for incoming data
    asio::streambuf request_buffer_;

    // Routing module for handling requests
    RoutingModule& routing_module_;

    int random_number_;

	bool keep_alive_;

    // The time when the connection was last active
    std::chrono::system_clock::time_point last_active_time_;

    static constexpr std::chrono::seconds IDLE_TIMEOUT_SECONDS = std::chrono::seconds(5);
};

#endif // CONNECTION_H
