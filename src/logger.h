#pragma once

#include <iostream>
#include <sstream>

#ifdef FOX_HTTP_DEBUG_LOG
#define FOX_HTTP_LOG(expr)                                               \
    do {                                                                   \
        std::ostringstream _hs_log_stream;                                 \
        _hs_log_stream << expr;                                            \
        std::cout << _hs_log_stream.str() << std::endl;                    \
    } while (0)
#else
#define FOX_HTTP_LOG(expr) do { } while (0)
#endif
