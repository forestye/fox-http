#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>
#include "connection.h"
#include "routing_module.h"
#include "timer_manager.h"

class Server : public std::enable_shared_from_this<Server> {
public:
    Server(unsigned short port, RoutingModule& routing_module);

    void run(unsigned int num_threads=0);

    void stop();

private:
    void accept();

    void handle_accept(std::shared_ptr<Connection> connection, const boost::system::error_code& error);

    unsigned short port_;
    RoutingModule& routing_module_;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> thread_pool_;
    TimerManager& timer_manager_ = TimerManager::instance();

};

#endif // SERVER_H
