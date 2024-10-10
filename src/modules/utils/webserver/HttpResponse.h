#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <string>
#include <map>

struct HttpResponse {
    std::string http_version;
    int status_code;
    std::string status_message;
    std::map<std::string, std::string> headers;
    std::string body;

    // Method to serialize the response to a string
    std::string to_string() const;
};

#endif // HTTPRESPONSE_H
