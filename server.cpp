#include "server.h"
#include <boost/bind/bind.hpp>
#include "connection.h"
#include "server_status.h"
#include "timer_manager.h"
#include <iostream>

using namespace std;
using boost::asio::ip::tcp;


/*
Server constructor receives a port number and a reference to a RoutingModule.
Initializes port_ and routing_module_ member variables and sets an acceptor_ to listen for connections on the specified port.
*/
Server::Server(unsigned short port, RoutingModule& routing_module)
    : port_(port), 
      routing_module_(routing_module), 
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) 
{
}

/*
run function starts the main functionality of the server.
First, it calls the accept function to start accepting new client connections.
Then, it creates a thread pool and runs io_context. The size of the thread pool equals the maximum number of concurrent threads supported by the system.
Finally, it waits for all threads in the thread pool to complete.
*/
void Server::run(unsigned int num_threads) {
    // Initialize and start the timer manager
    TimerManager& timer_manager = TimerManager::instance();
    timer_manager.init(io_context_, std::chrono::seconds(3)); 
    timer_manager.start_timer();

    // Start accept loop
    accept();

    // Create a thread pool and run io_context
    if(num_threads==0) {
        num_threads = std::thread::hardware_concurrency()-1;
        if(num_threads<1) num_threads = 1;
    }
    cout<<"num_threads="<<num_threads<<endl;
    for (unsigned int i = 0; i < num_threads; ++i) {
        thread_pool_.emplace_back([this]() { io_context_.run(); });
    }

    // Join threads
    for (auto& thread : thread_pool_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    thread_pool_.clear();
}

/*
The stop function is used to shut down the server.
It first closes the acceptor_ to stop accepting new connections,
then cancels all asynchronous operations of temporary connections,
stops the io_context, and waits for the threads in the thread pool to complete.
*/
void Server::stop() {
    // Stop the timer manager
    TimerManager::instance().stop_timer();

    // Stop accepting new connections
    acceptor_.close();

    // Stop the io_context
    io_context_.stop();
}

/*
The accept function uses async_accept to asynchronously accept new client connections.
When a new connection is accepted, the callback function of async_accept will be called.
In the callback function, it first checks for errors. If there are no errors, it calls connection_->start() to start communication with the client.
Then, if the acceptor_ is still open, it recursively calls the accept() function to continue accepting other new connections.
*/
void Server::accept() {
    auto connection = std::make_shared<Connection>(io_context_, routing_module_);

    try {
        acceptor_.async_accept(connection->socket(),
                           [self = shared_from_this(), connection](const boost::system::error_code& ec) {
                               self->handle_accept(connection,ec);
                           });
    } catch (const std::bad_weak_ptr& e) {
        cout << "Exception: " << e.what() << endl;
    }
}

/*
The handle_accept function is a callback function for handling connections.
It receives a Connection pointer and an error code.
If there are no errors, it calls connection->start() to start communication with the client.
Then, whether or not there are errors, it continues to call the accept() function to accept other new connections.
*/
void Server::handle_accept(std::shared_ptr<Connection> connection, const boost::system::error_code& error) {
    if (!error) {
        // Add the connection to the timer manager
        timer_manager_.add_connection(connection,connection->get_id());
        connection->start();
        //ServerStatus::instance().increment_connection_count();
    }

    // Continue accepting connections
    accept();
}