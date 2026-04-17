#pragma once

#include <iostream>
#include <sstream>

#ifdef HTTPSERVER_DEBUG_LOG
#define HTTPSERVER_LOG(expr)                                               \
    do {                                                                   \
        std::ostringstream _hs_log_stream;                                 \
        _hs_log_stream << expr;                                            \
        std::cout << _hs_log_stream.str() << std::endl;                    \
    } while (0)
#else
#define HTTPSERVER_LOG(expr) do { } while (0)
#endif
