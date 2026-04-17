#pragma once

namespace httpserver {

class HttpRequest;
class HttpResponse;

class HttpHandler {
public:
    virtual ~HttpHandler() = default;

    virtual void handle(HttpRequest& req, HttpResponse& resp) = 0;
};

}  // namespace httpserver
