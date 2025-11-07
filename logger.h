#pragma once

#include <iostream>
#include <sstream>

#ifdef ENABLE_DEBUG_LOG
#define DEBUG_LOG(expr)                                                    \
    do {                                                                   \
        std::ostringstream _debug_log_stream;                              \
        _debug_log_stream << expr;                                         \
        std::cout << _debug_log_stream.str() << std::endl;                 \
    } while (0)
#else
#define DEBUG_LOG(expr) do { } while (0)
#endif

