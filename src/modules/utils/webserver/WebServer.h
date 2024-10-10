#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "libs/Module.h"
#include "WifiProvider.h"
#include "TcpServer.h"
#include "HttpResponse.h"
#include "Endpoint.h"
#include <string>
#include <map>
#include <functional>
#include <vector>

// Forward declaration of HttpRequestHandler
class HttpRequestHandler;

class WebServer : public Module {
public:
    WebServer(WifiProvider* wifi_provider);
    void on_module_loaded();

    void register_handler(HttpRequestHandler* handler);

    bool send_data(const Endpoint& endpoint, const std::string& data);
    bool close_connection(const Endpoint& endpoint);
    bool send_http_response(const Endpoint& endpoint, const HttpResponse& response);

    std::string url_decode(const std::string& str);

private:
    TcpServer* tcpserver;

    // Connection state structure
    struct ConnectionState {
        std::string buffer;
        bool request_line_parsed = false;
        std::string method;
        std::string uri;
        std::string http_version;
        HttpRequestHandler* handler = nullptr;
    };

    WifiProvider* wifi_provider;

    // Map of active connections (keyed by connection identifier)
    std::map<Endpoint, ConnectionState> connections;

    // Registered request handlers
    std::vector<HttpRequestHandler*> handlers;

    // Helper method to parse the request line
    bool parse_request_line(ConnectionState& conn_state, const std::string& request_line);
    void on_data_received(const Endpoint& endpoint, const std::string& data);
};

#endif // WEBSERVER_H
