#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <string>
#include <cstdint>
#include <cstring>   // For memcpy
#include <cstdio>    // For snprintf

/**
 * @brief Class representing a network endpoint with IP address and port.
 */
class Endpoint {
public:
    uint8_t ip[4];      /**< IPv4 address, e.g., {192, 168, 1, 1} */
    uint16_t port;      /**< Port number, e.g., 8080 */

    /**
     * @brief Constructor that initializes the Endpoint with an IP address and port.
     *
     * @param ip_bytes Pointer to a 4-byte array representing the IPv4 address.
     * @param p Port number.
     */
    Endpoint(const uint8_t ip_bytes[4], uint16_t p) {
        std::memcpy(ip, ip_bytes, 4);
        port = p;
    }

    /**
     * @brief Converts the Endpoint to a C-style string in "IP:Port" format.
     *
     * @return A pointer to a null-terminated C-style string representing the Endpoint, e.g., "192.168.1.1:8080".
     *
     * @note The returned pointer remains valid as long as the Endpoint instance exists.
     */
    const char* to_c_string() const {
        std::snprintf(addr_, sizeof(addr_), "%u.%u.%u.%u:%u",
                      ip[0], ip[1], ip[2], ip[3],
                      port);
        return addr_;
    }

    /**
     * @brief Overloads the less-than operator to enable Endpoint objects to be used as keys in std::map.
     *
     * @param other Another Endpoint object to compare against.
     * @return true if this Endpoint is less than the other Endpoint.
     * @return false otherwise.
     */
    bool operator<(const Endpoint& other) const {
        for (int i = 0; i < 4; ++i) {
            if (ip[i] < other.ip[i]) return true;
            if (ip[i] > other.ip[i]) return false;
        }
        return port < other.port;
    }

    /**
     * @brief Overloads the equality operator to compare two Endpoint objects.
     *
     * @param other Another Endpoint object to compare against.
     * @return true if both IP addresses and ports are identical.
     * @return false otherwise.
     */
    bool operator==(const Endpoint& other) const {
        return (std::memcmp(ip, other.ip, 4) == 0) && (port == other.port);
    }

private:
    mutable char addr_[22]; /**< Buffer to store the formatted "IP:Port" string. */
};

#endif // ENDPOINT_H
