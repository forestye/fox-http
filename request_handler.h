#pragma once

#include <iostream>
#include <string>
#include "http_request.h"
#include "http_response.h"

class BaseRequestHandler {
public:
    virtual void operator()(const HttpRequest &request, HttpResponse &response) = 0;

protected:
    void set_common_response_headers(HttpResponse &response);
};

class GetRequestHandler : public BaseRequestHandler {
public:
    void operator()(const HttpRequest &request, HttpResponse &response) override ;

protected:
    virtual void handle_get(const HttpRequest &request, HttpResponse &response) = 0;
};

class PostRequestHandler : public BaseRequestHandler {
public:
    void operator()(const HttpRequest &request, HttpResponse &response) override ;

protected:
    virtual void handle_post(const HttpRequest &request, HttpResponse &response) = 0;
};
