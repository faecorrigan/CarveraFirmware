#include "HelloWorldHandler.h"
#include "HttpResponse.h"

/**
 * @brief Constructor that initializes the handler with a reference to the WebServer.
 * @param web_server Pointer to the WebServer instance.
 */
HelloWorldHandler::HelloWorldHandler(WebServer* web_server)
    : SimpleRequestHandler(web_server) {}

/**
 * @brief Destructor.
 */
HelloWorldHandler::~HelloWorldHandler() {}

/**
 * @brief Determine if this handler can process the given request.
 * @param method The HTTP method (e.g., GET, POST).
 * @param uri The request URI.
 * @param http_version The HTTP version (e.g., HTTP/1.1).
 * @return true if the handler can process the request, false otherwise.
 */
bool HelloWorldHandler::can_handle(const std::string& method, const std::string& uri, const std::string& http_version) {
    return (method == "GET" && uri == "/hello");
}

/**
 * @brief Process the complete HTTP request.
 *
 * Constructs and sends an appropriate response based on the presence of a request body.
 *
 * @param endpoint The client endpoint.
 * @param method The HTTP method.
 * @param uri The request URI.
 * @param headers The parsed HTTP headers.
 * @param body The request body.
 * @return true if the connection should remain open (for long-running connections),
 *         false if the connection should be closed after processing.
 */
bool HelloWorldHandler::process_request(const Endpoint& endpoint,
                                        const std::string& method,
                                        const std::string& uri,
                                        const std::map<std::string, std::string>& headers,
                                        const std::string& body) {
    THEKERNEL->streams->printf("process_request: Processing request for endpoint %s, Method: %s, URI: %s, Body length: %i\n",
                               endpoint.to_c_string(), method.c_str(), uri.c_str(), static_cast<int>(body.size()));

    // Debugging headers
    THEKERNEL->streams->printf("process_request: Headers received:\n");
    for (const auto& header : headers) {
        THEKERNEL->streams->printf("  %s: %s\n", header.first.c_str(), header.second.c_str());
    }

    std::string response_body;

    if (!body.empty()) {
        response_body = "Hello " + body + "!";
        THEKERNEL->streams->printf("process_request: Non-empty body received, constructing response: %s\n", response_body.c_str());
    } else {
        response_body = "Hello world!";
        THEKERNEL->streams->printf("process_request: Empty body, response set to: %s\n", response_body.c_str());
    }

    // Prepare the HTTP response
    HttpResponse response;
    response.http_version = "HTTP/1.1";
    response.status_code = 200;
    response.status_message = "OK";
    response.headers["Content-Type"] = "text/plain";
    response.headers["Content-Length"] = std::to_string(response_body.size());
    response.headers["Connection"] = "close";
    response.body = response_body;

    THEKERNEL->streams->printf("process_request: Prepared HTTP response, Body length: %i\n", static_cast<int>(response_body.size()));

    // Send the response
    send_http_response(endpoint, response);
    THEKERNEL->streams->printf("process_request: Response sent successfully to endpoint %s\n", endpoint.to_c_string());

    // Indicate that the connection should be closed
    THEKERNEL->streams->printf("process_request: Finished processing request for endpoint %s, connection will be closed\n", endpoint.to_c_string());
    return false;
}

