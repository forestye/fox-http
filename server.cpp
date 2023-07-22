#include "server.h"
#include <boost/bind/bind.hpp>
#include "connection.h"
#include <iostream>
using namespace std;
using boost::asio::ip::tcp;


/*
在这里，服务器构造函数接收一个端口号和一个 RoutingModule 类型的引用。
构造函数初始化 port_ 和 routing_module_ 成员变量，并设置一个接受器 acceptor_ 监听指定端口上的连接。
*/
Server::Server(unsigned short port, RoutingModule& routing_module)
    : port_(port), 
      routing_module_(routing_module), 
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) 
{
}

/*
run 函数启动服务器的主要功能。
首先调用 accept 函数来开始接受新的客户端连接。
然后创建一个线程池并运行 io_context。线程池的大小等于系统支持的最大并发线程数。
最后，等待线程池中的所有线程完成。
*/
void Server::run(unsigned int num_threads) {

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
stop 函数用于关闭服务器。
它首先关闭 acceptor_ 以停止接受新连接，
然后取消所有临时连接的异步操作，
停止 io_context 并等待线程池中的线程完成。
*/
void Server::stop() {
    // Stop accepting new connections
    acceptor_.close();

    // Stop the io_context
    io_context_.stop();

}

/*
accept 函数使用 async_accept 异步地接受新的客户端连接。
当一个新的连接被接受时，async_accept 的回调函数将被调用。
在回调函数中，首先检查是否有错误。如果没有错误，就调用 connection_->start() 来启动与客户端的通信。
然后，如果 acceptor_ 仍处于打开状态，递归调用 accept() 函数继续接受其他新连接。
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
handle_accept 函数是一个处理连接的回调函数。
它接收一个 Connection 指针和一个错误码。
如果没有错误，就调用 connection->start() 来启动与客户端的通信。
然后，无论是否有错误，它都会继续调用 accept() 函数接受其他新连接。
*/
void Server::handle_accept(std::shared_ptr<Connection> connection, const boost::system::error_code& error) {
    if (!error) {
        connection->start();
    }

    // Continue accepting connections
    accept();
}
