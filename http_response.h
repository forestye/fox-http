/*
在这个头文件中，我们定义了一个名为HttpResponse的类，它表示一个HTTP响应。
HttpResponse类提供了用于设置响应属性（如状态码、原因短语、头字段和响应体）的方法，以及将响应转换为字符串的方法。
我们使用std::map来存储响应头字段。
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
