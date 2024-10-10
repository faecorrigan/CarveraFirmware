#include "TcpServer.h"
#include "WifiProvider.h"
#include "Logging.h"
#include "Endpoint.h"
#include <cstring>
#include <string>

TcpServer::TcpServer(WifiProvider* provider, uint16_t localPort)
    : wifiProvider(provider), local_port(localPort), data_callback(nullptr)
{
    uint8_t maxClients = 15;

    if (wifiProvider) {
        // Initialize the TCP server via WifiProvider
        link_no = wifiProvider->initializeTcpServer(localPort, maxClients);
        if (link_no == 0xFF) {
            printk("TcpServer: Failed to initialize TCP server on port %d\n", localPort);
        } else {
            // Bind the intermediary callback function
            auto callback = std::bind(&TcpServer::onWifiDataReceived, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2,
                                      std::placeholders::_3,
                                      std::placeholders::_4);

            // Register the callback with WifiProvider
            wifiProvider->registerTcpDataCallback(link_no, callback);

            printk("TcpServer: Initialized TCP server on port %d with link_no %d\n", localPort, link_no);
        }
    } else {
        printk("TcpServer: WifiProvider is null\n");
        link_no = 0xFF;
    }
}

TcpServer::~TcpServer()
{
    if (wifiProvider && link_no != 0xFF) {
        wifiProvider->removeTcpServer(link_no);
        printk("TcpServer: Destroyed TCP server with link_no %d\n", link_no);
    }
}

void TcpServer::registerDataCallback(DataCallback callback)
{
    data_callback = callback;
}

bool TcpServer::sendData(const Endpoint& endpoint, const std::string& data)
{
    if (wifiProvider && link_no != 0xFF) {
        bool success = wifiProvider->sendTcpDataToClient(
            endpoint.ip,
            endpoint.port,
            link_no,
            reinterpret_cast<const uint8_t*>(data.c_str()),
            data.length()
        );

        if (!success) {
            printk("TcpServer: Failed to send data to %s\n", endpoint.to_c_string());
        } else {
            printk("TcpServer: Sent data to %s\n", endpoint.to_c_string());
        }

        return success;
    }
    printk("TcpServer: Cannot send data, WifiProvider or link_no invalid\n");
    return false;
}

bool TcpServer::closeConnection(const Endpoint& endpoint)
{
    if (wifiProvider && link_no != 0xFF) {
        bool success = wifiProvider->closeTcpConnection(
            endpoint.ip,
            endpoint.port,
            link_no
        );

        if (!success) {
            printk("TcpServer: Failed to close connection to %s\n", endpoint.to_c_string());
        } else {
            printk("TcpServer: Closed connection to %s\n", endpoint.to_c_string());
        }

        return success;
    }
    printk("TcpServer: Cannot close connection, WifiProvider or link_no invalid\n");
    return false;
}

void TcpServer::onWifiDataReceived(uint8_t* ip, uint16_t port, uint8_t* data, uint16_t length)
{
    Endpoint endpoint(ip, port);
    std::string dataStr(reinterpret_cast<char*>(data), length);

    if (data_callback) {
        printk("TcpServer: Received data from %s\n", endpoint.to_c_string());
        data_callback(endpoint, dataStr);
    } else {
        printk("TcpServer: No data callback registered\n");
    }
}
