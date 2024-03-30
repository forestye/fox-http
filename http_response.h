/*
Defines HttpResponse class representing an HTTP response.
HttpResponse class provides methods for setting response properties (status code, reason phrase, header fields, body) and converting response to a string.
Header fields are stored using std::map.
*/
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <map>
#include <string>

class HttpResponse {
public:
    // Set the status code and reason phrase
    void set_status_code(int status_code);
    void set_reason_phrase(const std::string& reason_phrase);

    // Set a header field
    void set_header(const std::string& name, const std::string& value);

    // Set the response body
    void set_body(const std::string& body);

    // Convert the response to a string
    std::string to_string() const;

private:
    // The status code
    int status_code_;

    // The reason phrase
    std::string reason_phrase_;

    // The response headers
    std::map<std::string, std::string> headers_;

    // The response body
    std::string body_;
};

#endif // HTTP_RESPONSE_H
