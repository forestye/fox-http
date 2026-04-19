#include "server_status.h"
#include <boost/lexical_cast.hpp>

std::string ServerStatus::get_status_str() const {
    std::string status_str = "Current connections: " + boost::lexical_cast<std::string>(get_connection_count()) + "<br>";
    status_str += "Max connections: " + boost::lexical_cast<std::string>(get_max_connection_count()) + "<br>";
    status_str += "Current connection objects: " + boost::lexical_cast<std::string>(get_connection_object_count()) + "<br>";
    status_str += "Max connection objects: " + boost::lexical_cast<std::string>(get_max_connection_object_count()) + "<br>";
    status_str += "Connection increments: " + boost::lexical_cast<std::string>(get_increment_count()) + "<br>";
    status_str += "Connection decrements: " + boost::lexical_cast<std::string>(get_decrement_count()) + "<br>";
    status_str += "Connection timeouts: " + boost::lexical_cast<std::string>(get_connection_timeout_count()) + "<br>"; // Add connection timeout count
    return status_str;
}

