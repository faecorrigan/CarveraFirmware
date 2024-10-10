#ifndef HTTPREQUESTHANDLER_H
#define HTTPREQUESTHANDLER_H

#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include "WebServer.h"
#include "HttpResponse.h"

/**
 * @brief Base class for handling HTTP requests.
 *
 * Provides helper functions for parsing HTTP headers and accessing header values.
 */
class HttpRequestHandler {
public:
    /**
     * Constructor that initializes the handler with a reference to the WebServer.
     * @param web_server Pointer to the WebServer instance.
     */
    HttpRequestHandler(WebServer* web_server)
        : web_server_(web_server) {}
    
    virtual ~HttpRequestHandler() {}

    /**
     * Determine if this handler can process the given request.
     * @param method The HTTP method (e.g., GET, POST).
     * @param uri The request URI.
     * @param http_version The HTTP version (e.g., HTTP/1.1).
     * @return true if the handler can process the request, false otherwise.
     */
    virtual bool can_handle(const std::string& method, const std::string& uri, const std::string& http_version) = 0;

    /**
     * Handle incoming data for a connection.
     * @param endpoint The client endpoint.
     * @param data Pointer to the incoming data buffer.
     * @param length Length of the incoming data.
     * @return true if the connection should remain open (for long-running connections),
     *         false if the connection should be closed after processing.
     */
    virtual bool handle_data(const Endpoint& endpoint, const std::string& data) = 0;

protected:
    /**
     * Send data to the client.
     * @param endpoint The client endpoint.
     * @param data The data to send.
     * @return true on success, false otherwise.
     */
    bool send_data(const Endpoint& endpoint, const std::string& data) {
        return web_server_->send_data(endpoint, data);
    }

    /**
     * Close the connection with the client.
     * @param endpoint The client endpoint.
     * @return true on success, false otherwise.
     */
    bool close_connection(const Endpoint& endpoint) {
        return web_server_->close_connection(endpoint);
    }

    /**
     * Send an HTTP response to the client.
     * @param endpoint The client endpoint.
     * @param response The HttpResponse object containing response details.
     * @return true on success, false otherwise.
     */
    bool send_http_response(const Endpoint& endpoint, const HttpResponse& response) {
        return web_server_->send_http_response(endpoint, response);
    }

    /**
     * @brief Trim leading and trailing whitespace from a string.
     * @param s The string to trim.
     */
    void trim(std::string& s) {
        // Trim leading whitespace
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        // Trim trailing whitespace
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    }

    /**
     * @brief Parse HTTP headers from a raw string.
     * @param headers_str The raw HTTP headers as a single string.
     * @return A map of header keys to values.
     */
    std::map<std::string, std::string> parse_headers(const std::string& headers_str) const {
        std::map<std::string, std::string> headers;
        size_t start = 0;
        size_t end;

        while ((end = headers_str.find("\r\n", start)) != std::string::npos) {
            // Extract the line
            std::string line = headers_str.substr(start, end - start);
            start = end + 2; // Move past "\r\n"

            if (line.empty()) {
                // Empty line indicates end of headers
                break;
            }

            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);

                // Trim whitespace from key and value
                trim(key);
                trim(value);

                // Convert header keys to lowercase for case-insensitive comparison
                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c){ return std::tolower(c); });

                headers[key] = value;
            }
        }

        return headers;
    }

    /**
     * @brief Retrieve the value of a specific header.
     * @param headers The map of parsed headers.
     * @param key The header key to retrieve.
     * @param default_value The default value to return if header is not found.
     * @return The header value if found, otherwise default_value.
     */
    std::string get_header_value(const std::map<std::string, std::string>& headers, const std::string& key, const std::string& default_value = "") const {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        auto it = headers.find(lower_key);
        if (it != headers.end()) {
            return it->second;
        }
        return default_value;
    }

    /**
     * @brief Safely convert a string to an unsigned long integer.
     * @param str The string to convert.
     * @param result Reference to store the conversion result.
     * @return true if conversion was successful, false otherwise.
     */
    bool safe_stoul(const std::string& str, unsigned long& result) const {
        errno = 0;
        char* endptr = nullptr;
        result = std::strtoul(str.c_str(), &endptr, 10);
        if (endptr == str.c_str() || *endptr != '\0' || errno == ERANGE) {
            return false;
        }
        return true;
    }

private:
    /**
     * @brief Trim whitespace from both ends of a string.
     * @param s The string to trim.
     */
    void trim(std::string& s) const {
        // Trim leading whitespace
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));

        // Trim trailing whitespace
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }

    WebServer* web_server_; // Pointer to the WebServer instance
};

#endif // HTTPREQUESTHANDLER_H
