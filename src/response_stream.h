#pragma once

#include <cstddef>
#include <functional>
#include <sys/uio.h>  // iovec

namespace fox::http {

// Internal abstraction for synchronous response writing. Implemented by
// Connection via a boost::asio::socket wrapper. Kept as a pure interface so
// HttpResponse stays decoupled from Boost.Asio types.
class ResponseStream {
public:
    virtual ~ResponseStream() = default;

    // Synchronous write. Returns true if all bytes were sent.
    virtual bool write(const void* data, std::size_t n) = 0;

    // Synchronous scatter-gather write. Returns true if all bytes were sent.
    virtual bool writev(const struct iovec* iov, int iovcnt) = 0;
};

// Interface for dispatching lambdas to the handler thread pool (Stream mode).
class StreamDispatcher {
public:
    virtual ~StreamDispatcher() = default;
    virtual void post(std::function<void()> fn) = 0;
};

}  // namespace fox::http
