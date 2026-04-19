#include "server_status.h"

#include <sstream>

namespace fox::http {

std::string ServerStatus::status_str() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "connections=" << connection_count_
       << " peak=" << max_connection_count_
       << " live_objects=" << connection_object_count_
       << " peak_objects=" << max_connection_object_count_
       << " timeouts=" << connection_timeout_count_;
    return ss.str();
}

}  // namespace fox::http
