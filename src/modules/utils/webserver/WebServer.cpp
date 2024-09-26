#include "WebServer.h"
#include "TcpServer.h"
#include "HttpRequestHandler.h"
#include "ConfigValue.h"
#include "Config.h"
#include "checksumm.h"
#include "utils.h"
#include "libs/Kernel.h"
#include "libs/Logging.h"
#include "libs/StreamOutput.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>

#include "HelloWorldHandler.h"
//#include "ConsoleStreamHandler.h"
//#include "StatusStreamHandler.h"

#define webserver_checksum            CHECKSUM("webserver")
#define webserver_enable_checksum     CHECKSUM("enable")
#define webserver_port_checksum       CHECKSUM("port")

WebServer::WebServer(WifiProvider* wifi_provider) : wifi_provider(wifi_provider)
{
}

void WebServer::on_module_loaded()
{
    // Check if WifiProvider instance is available
    if (!wifi_provider) {
        printk("WebServer: WifiProvider is null!\n");
        return;
    }

    // Load the webserver port from the configuration
    uint16_t webserver_port = THEKERNEL->config->value(webserver_checksum, webserver_port_checksum)->by_default(80)->as_int();

    tcpserver = new TcpServer(wifi_provider, webserver_port);

    // Register callback for data reception
    tcpserver->registerDataCallback([this](const Endpoint& endpoint, const std::string& data) {
        this->on_data_received(endpoint, data);
    });

    HelloWorldHandler* hello_handler = new HelloWorldHandler(this);
    register_handler(hello_handler);

//    ConsoleStreamHandler* console_handler = new ConsoleStreamHandler(this);
//    register_handler(console_handler);

//    StatusStreamHandler* status_handler = new StatusStreamHandler(this);
//    register_handler(status_handler);
}

void WebServer::on_data_received(const Endpoint& endpoint, const std::string& data)
{
    // Get or create the connection state
    ConnectionState& conn_state = connections[endpoint];

    // Append received data to the buffer
    conn_state.buffer.append(data);

    // If request line is not yet parsed, attempt to parse it
    if (!conn_state.request_line_parsed) {
        size_t pos = conn_state.buffer.find("\r\n");
        if (pos == std::string::npos) {
            // Request line is incomplete
            return; // Wait for more data
        }

        // Extract the request line
        std::string request_line = conn_state.buffer.substr(0, pos);

        // Remove the request line from the buffer
        conn_state.buffer.erase(0, pos + 2); // +2 to remove "\r\n"

        // Parse the request line
        if (!parse_request_line(conn_state, request_line)) {
            // Malformed request line
            HttpResponse response;
            response.http_version = "HTTP/1.1";
            response.status_code = 400;
            response.status_message = "Bad Request";
            response.headers["Content-Type"] = "text/plain";
            response.headers["Connection"] = "close";
            response.body = "400 Bad Request";
            send_http_response(endpoint, response);
            connections.erase(endpoint);
            close_connection(endpoint);
            return;
        }

        conn_state.request_line_parsed = true;

        // Select the appropriate handler
        HttpRequestHandler* selected_handler = nullptr;
        for (auto handler : handlers) {
            if (handler->can_handle(conn_state.method, conn_state.uri, conn_state.http_version)) {
                selected_handler = handler;
                break;
            }
        }

        if (!selected_handler) {
            // No handler found, send 404 Not Found
            HttpResponse response;
            response.http_version = "HTTP/1.1";
            response.status_code = 404;
            response.status_message = "Not Found";
            response.headers["Content-Type"] = "text/plain";
            response.headers["Connection"] = "close";
            response.body = "404 Not Found";
            send_http_response(endpoint, response);
            connections.erase(endpoint);
            close_connection(endpoint);
            return;
        }

        // Assign the handler to the connection
        conn_state.handler = selected_handler;

        // Delegate all remaining data to the handler
        if (conn_state.buffer.empty()) {
            // No more data to process right now
            return;
        }

        bool keep_connection = conn_state.handler->handle_data(endpoint, conn_state.buffer);

        // Clear the buffer as it's been delegated
        conn_state.buffer.clear();

        if (!keep_connection) {
            // Handler indicates to close the connection
            connections.erase(endpoint);
            close_connection(endpoint);
        } else {
            // Handler manages the connection (long-running)
        }

    } else {
        // Request line already parsed, delegate all incoming data to the handler
        if (conn_state.handler) {
            bool keep_connection = conn_state.handler->handle_data(endpoint, data);
            if (!keep_connection) {
                // Handler indicates to close the connection
                connections.erase(endpoint);
                close_connection(endpoint);
            } else {
                // Handler manages the connection (long-running)
            }
        } else {
            // No handler assigned; close the connection for safety
            connections.erase(endpoint);
            close_connection(endpoint);
        }
    }
}

bool WebServer::parse_request_line(ConnectionState& conn_state, const std::string& request_line) {
    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos) {
        // Malformed request line
        return false;
    }

    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos) {
        // Malformed request line
        return false;
    }

    // Extract method, URI, and HTTP version
    conn_state.method = request_line.substr(0, first_space);
    conn_state.uri = request_line.substr(first_space + 1, second_space - first_space - 1);
    conn_state.http_version = request_line.substr(second_space + 1);

    // Optionally, convert method and URI to uppercase or perform validation
    std::transform(conn_state.method.begin(), conn_state.method.end(), conn_state.method.begin(), ::toupper);

    return true;
}

bool WebServer::send_data(const Endpoint& endpoint, const std::string& data)
{
    return tcpserver->sendData(endpoint, data);
}

bool WebServer::close_connection(const Endpoint& endpoint)
{
    return tcpserver->closeConnection(endpoint);
}

bool WebServer::send_http_response(const Endpoint& endpoint, const HttpResponse& response)
{
    // Convert HttpResponse to string
    std::string response_str = response.to_string();

    // Send the response data
    return send_data(endpoint, response_str);
}

std::string HttpResponse::to_string() const
{
    std::string response_str;
    response_str += http_version + " " + std::to_string(status_code) + " " + status_message + "\r\n";
    for(const auto& header : headers) {
        response_str += header.first + ": " + header.second + "\r\n";
    }
    response_str += "\r\n";
    response_str += body;
    return response_str;
}

std::string WebServer::url_decode(const std::string& str)
{
    std::string decoded;
    char ch;
    size_t i;
    int ii;
    for (i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
                ch = static_cast<char>(ii);
                decoded += ch;
                i = i + 2;
            }
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

void WebServer::register_handler(HttpRequestHandler* handler)
{
    handlers.push_back(handler);
}
