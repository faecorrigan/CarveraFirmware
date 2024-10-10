#ifndef TCPSERVER_H_
#define TCPSERVER_H_

#include <functional>
#include <cstdint>
#include <string>

#include "Endpoint.h"

// Forward declaration to avoid circular dependency
class WifiProvider;

class TcpServer {
public:
    using DataCallback = std::function<void(const Endpoint &endpoint, const std::string& data)>;

    /**
     * @brief Constructs a TcpServer instance.
     * 
     * @param provider Pointer to the WifiProvider managing this server.
     * @param localPort The port number on which the server listens.
     */
    TcpServer(WifiProvider* provider, uint16_t localPort);

    /**
     * @brief Destructs the TcpServer instance and cleans up resources.
     */
    ~TcpServer();

    /**
     * @brief Registers a callback function to handle incoming data from clients.
     * 
     * @param callback The callback function to be invoked upon data reception.
     */
    void registerDataCallback(DataCallback callback);

    /**
     * @brief Sends data to a specific client identified by their IP and port.
     * 
     * @param remoteIp The IP address of the target client.
     * @param remotePort The port number of the target client.
     * @param data The data to send.
     * @return true If data was successfully sent.
     * @return false If sending failed.
     */
    bool sendData(const Endpoint &endpoint, const std::string& data);

    /**
     * @brief Closes the connection with a specific client identified by their IP and port.
     * 
     * @param remoteIp The IP address of the client.
     * @param remotePort The port number of the client.
     * @return true If the connection was successfully closed.
     * @return false If closing the connection failed.
     */
    bool closeConnection(const Endpoint &endpoint);

    /**
     * @brief Retrieves the local port on which the server is listening.
     * 
     * @return uint16_t The local port number.
     */
    uint16_t getLocalPort() const { return local_port; }

private:
    WifiProvider* wifiProvider;       // Pointer to the managing WifiProvider
    uint8_t link_no;                  // Internal link number managed by WifiProvider
    uint16_t local_port;              // Port number for the TCP server
    DataCallback data_callback;       // Callback for incoming data

    void onWifiDataReceived(uint8_t* ip, uint16_t port, uint8_t* data, uint16_t length);

    // Disable copy constructor and assignment
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
};

#endif /* TCPSERVER_H_ */
