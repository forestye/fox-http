#include "timer_manager.h"
#include "connection.h"
#include "server_status.h"
#include <iostream>
using namespace std;

void TimerManager::add_connection(std::shared_ptr<Connection> connection,size_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[id] = std::weak_ptr<Connection>(connection);
	ServerStatus::instance().set_connection_object_count(connections_.size());
}

void TimerManager::remove_connection(size_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(id);
	ServerStatus::instance().set_connection_object_count(connections_.size());
}

void TimerManager::start_timer() {
    if (!timer_) {
        std::cout << "TimerManager not initialized. Call TimerManager::init before starting the timer." << std::endl;
        return;
    }
	timer_->expires_after(interval_);
    timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            check_idle_connections();
        }
    });
}

void TimerManager::stop_timer() {
    if (timer_) {
        timer_->cancel();
    }
}

void TimerManager::check_idle_connections() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (std::shared_ptr<Connection> connection = it->second.lock()) {
            if (connection->is_idle()) {
                connection->cancel();
				cout<<"connection timeout:"<<connection->get_id()<<endl;
				ServerStatus::instance().increment_connection_timeout_count();
            } 
        }
		++it;
    }

	ServerStatus::instance().set_connection_object_count(connections_.size());

	it = connections_.begin();
    while (it != connections_.end()) {
        if (std::shared_ptr<Connection> connection = it->second.lock()) {
			++it;
		} else {
            it = connections_.erase(it);
        }
    }

    ServerStatus::instance().set_connection_object_count(connections_.size());
	
    start_timer();

}
