#include "request_handler.h"

void BaseRequestHandler::set_common_response_headers(HttpResponse &response) {
    response.set_status_code(200);
    response.set_reason_phrase("OK");
    response.set_header("Content-Type", "text/html; charset=utf-8");
}

void GetRequestHandler::operator()(const HttpRequest &request, HttpResponse &response) {
    if (request.get_method() == HttpRequest::Method::GET) {
        handle_get(request, response);
    } else {
        response.set_status_code(405);
        response.set_reason_phrase("Method Not Allowed");
        response.set_header("Allow", "GET");
    }
}

void PostRequestHandler::operator()(const HttpRequest &request, HttpResponse &response) {
    if (request.get_method() == HttpRequest::Method::POST) {
        handle_post(request, response);
    } else {
        response.set_status_code(405);
        response.set_reason_phrase("Method Not Allowed");
        response.set_header("Allow", "POST");
    }
}
