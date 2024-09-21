#ifndef HELLOWORLDHANDLER_H
#define HELLOWORLDHANDLER_H

#include "SimpleRequestHandler.h"

/**
 * @brief Handler for the /hello endpoint.
 *
 * Responds with "Hello world!" or "Hello $body!" based on the presence of a request body.
 */
class HelloWorldHandler : public SimpleRequestHandler {
public:
    /**
     * @brief Constructor that initializes the handler with a reference to the WebServer.
     * @param web_server Pointer to the WebServer instance.
     */
    HelloWorldHandler(WebServer* web_server);

    /**
     * @brief Destructor.
     */
    virtual ~HelloWorldHandler();

    /**
     * @brief Determine if this handler can process the given request.
     * @param method The HTTP method (e.g., GET, POST).
     * @param uri The request URI.
     * @param http_version The HTTP version (e.g., HTTP/1.1).
     * @return true if the handler can process the request, false otherwise.
     */
    virtual bool can_handle(const std::string& method, const std::string& uri, const std::string& http_version) override;

protected:
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
    virtual bool process_request(const Endpoint& endpoint,
                                 const std::string& method,
                                 const std::string& uri,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& body) override;
};

#endif // HELLOWORLDHANDLER_H
