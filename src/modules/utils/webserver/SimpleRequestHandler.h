#ifndef SIMPLEREQUESTHANDLER_H
#define SIMPLEREQUESTHANDLER_H

#include "HttpRequestHandler.h"
#include "Endpoint.h"
#include "libs/Kernel.h"
#include "libs/Logging.h"

/**
 * @brief Base class for handling simple HTTP requests.
 *
 * This class manages the parsing of HTTP headers and bodies, providing a simplified interface
 * for derived classes to implement specific request handling logic.
 */
class SimpleRequestHandler : public HttpRequestHandler {
public:
    /**
     * Constructor that initializes the handler with a reference to the WebServer.
     * @param web_server Pointer to the WebServer instance.
     */
    SimpleRequestHandler(WebServer* web_server)
        : HttpRequestHandler(web_server) {}
    
    virtual ~SimpleRequestHandler() {}

/**
 * Handle incoming data for a connection.
 * @param endpoint The client endpoint.
 * @param data Pointer to the incoming data buffer.
 * @param length Length of the incoming data.
 * @return true if the connection should remain open (for long-running connections),
 *         false if the connection should be closed after processing.
 */
virtual bool handle_data(const Endpoint& endpoint, const char* data, size_t length) override {
    printk("handle_data: Received data for endpoint %s, Length: %i\n", endpoint.to_c_string(), static_cast<int>(length));

    ConnectionState& state = connection_states_[endpoint];

    // Append incoming data to the buffer
    state.buffer.append(data, length);
    printk("handle_data: Appended data to buffer, Current buffer size: %i\n", static_cast<int>(state.buffer.size()));

    // If headers are not yet parsed, attempt to parse them
    if (!state.headers_parsed) {
        printk("handle_data: Attempting to parse headers for endpoint %s\n", endpoint.to_c_string());
        size_t headers_end = state.buffer.find("\r\n\r\n");
        if (headers_end != std::string::npos) {
            // Headers are complete
            printk("handle_data: Found end of headers at position %i\n", static_cast<int>(headers_end));
            std::string headers_str = state.buffer.substr(0, headers_end + 2); // Include the last \r\n
            state.headers = parse_headers(headers_str);
            printk("handle_data: Parsed headers for endpoint %s\n", endpoint.to_c_string());

            // Determine if there's a body based on Content-Length
            std::string content_length_str = get_header_value(state.headers, "Content-Length", "0");
            unsigned long content_length = 0;
            if (!safe_stoul(content_length_str, content_length)) {
                // Invalid Content-Length, respond with 400 Bad Request
                printk("handle_data: Invalid Content-Length for %s: %s\n", endpoint.to_c_string(), content_length_str.c_str());
                HttpResponse response;
                response.http_version = "HTTP/1.1";
                response.status_code = 400;
                response.status_message = "Bad Request";
                response.headers["Content-Type"] = "text/plain";
                response.headers["Connection"] = "close";
                response.body = "400 Bad Request";
                send_http_response(endpoint, response);
                close_connection(endpoint);
                connection_states_.erase(endpoint);
                return false;
            }

            state.content_length = content_length;
            state.headers_parsed = true;
            printk("handle_data: Content-Length parsed for %s, Content-Length: %i\n", endpoint.to_c_string(), static_cast<int>(state.content_length));

            // Remove headers from the buffer
            state.buffer.erase(0, headers_end + 4); // Remove "\r\n\r\n"
            printk("handle_data: Headers removed from buffer for endpoint %s, Remaining buffer size: %i\n", endpoint.to_c_string(), static_cast<int>(state.buffer.size()));
        } else {
            // Wait for more data
            printk("handle_data: Headers not complete for endpoint %s, waiting for more data\n", endpoint.to_c_string());
            return true;
        }
    }

    // If headers are parsed and there's a body, check if it's fully received
    if (state.headers_parsed && state.content_length > 0) {
        printk("handle_data: Checking if full body is received for endpoint %s\n", endpoint.to_c_string());
        if (state.buffer.size() >= state.content_length) {
            // Full body is received
            printk("handle_data: Full body received for endpoint %s, Buffer size: %i, Content-Length: %i\n", endpoint.to_c_string(), static_cast<int>(state.buffer.size()), static_cast<int>(state.content_length));
            
            // Capture and debug the body contents before storing them
            state.body = state.buffer.substr(0, state.content_length);
            printk("handle_data: Extracted body for endpoint %s, Body size: %i\n", endpoint.to_c_string(), static_cast<int>(state.body.size()));
  
            // Confirm the body content (print the first 100 characters)
            if (state.body.size() > 100) {
                printk("handle_data: First 100 chars of body: %.100s\n", state.body.c_str());
            } else {
                printk("handle_data: Full body: %s\n", state.body.c_str());
            }

            // Remove body from buffer
            state.buffer.erase(0, state.content_length);
            printk("handle_data: Body removed from buffer, Remaining buffer size: %i\n", static_cast<int>(state.buffer.size()));

            // Process the complete request
            bool keep_connection = process_request(endpoint, state.method, state.uri, state.headers, state.body);
            
            // Cleanup
            printk("handle_data: Request processed for endpoint %s, Connection will be %s\n", endpoint.to_c_string(), keep_connection ? "kept open" : "closed");
            connection_states_.erase(endpoint);
            return keep_connection;
        } else {
            // Body is incomplete, wait for more data
            printk("handle_data: Incomplete body for endpoint %s, waiting for more data, Current buffer size: %i, Expected size: %i\n", endpoint.to_c_string(), static_cast<int>(state.buffer.size()), static_cast<int>(state.content_length));
            return true;
        }
    }

    // If headers are parsed but no body is expected
    if (state.headers_parsed && state.content_length == 0) {
        printk("handle_data: No body expected for endpoint %s, processing request\n", endpoint.to_c_string());
        // Process the request with an empty body
        bool keep_connection = process_request(endpoint, state.method, state.uri, state.headers, "");
        
        // Cleanup
        printk("handle_data: Request processed for endpoint %s, Connection will be %s\n", endpoint.to_c_string(), keep_connection ? "kept open" : "closed");
        connection_states_.erase(endpoint);
        return keep_connection;
    }

    // Incomplete body, wait for more data
    printk("handle_data: Waiting for more data for endpoint %s, Current buffer size: %i\n", endpoint.to_c_string(), static_cast<int>(state.buffer.size()));
    return true;
}

protected:
    /**
     * @brief Process the complete HTTP request.
     *
     * Derived classes should implement this method to handle the business logic
     * associated with the request.
     *
     * @param endpoint The client endpoint.
     * @param method The HTTP method.
     * @param uri The request URI.
     * @param headers The parsed HTTP headers.
     * @param body The request body.
     * @return true if the connection should remain open (for long-running connections),
     *         false if the connection should be closed after processing.
     */
    virtual bool process_request(const Endpoint& endpoint, const std::string& method, const std::string& uri, const std::map<std::string, std::string>& headers, const std::string& body) = 0;

private:
    /**
     * @brief Structure to maintain per-connection parsing state.
     */
    struct ConnectionState {
        std::string buffer;                  // Buffer to accumulate incoming data
        bool headers_parsed = false;         // Flag indicating if headers have been parsed
        unsigned long content_length = 0;    // Expected length of the request body
        std::map<std::string, std::string> headers; // Parsed headers
        std::string body;                    // Request body
        std::string method;                  // HTTP method (e.g., GET)
        std::string uri;                     // Request URI
        std::string http_version;            // HTTP version (e.g., HTTP/1.1)
    };

    // Map to maintain state for each connection, keyed by Endpoint
    std::map<Endpoint, ConnectionState> connection_states_;
};

#endif // SIMPLEREQUESTHANDLER_H
