/*
 * WifiProvider.h
 *
 *  Created on: 2020年6月10日
 *      Author: josh
 */

#ifndef WIFIPROVIDER_H_
#define WIFIPROVIDER_H_

using namespace std;
#include <vector>
#include <queue>
#include <map>
#include <functional>

#include "Pin.h"
#include "Module.h"
#include "StreamOutput.h"

#include "M8266WIFIDrv.h"
#include "libs/RingBuffer.h"

#define WIFI_DATA_MAX_SIZE 1460
#define WIFI_DATA_TIMEOUT_MS 10
#define MAX_WLAN_SIGNALS 8

class WifiProvider : public Module, public StreamOutput
{
public:
    void on_module_loaded();
    void on_gcode_received(void *argument);
    void on_main_loop( void* argument );
    void on_second_tick(void* argument);
    void on_idle(void* argument);
    void on_get_public_data(void* argument);
    void on_set_public_data(void* argument);

    uint8_t initializeTcpServer(uint16_t local_port, uint8_t max_clients);
    void removeTcpServer(uint8_t link_no);
    void registerTcpDataCallback(uint8_t link_no, std::function<void(uint8_t*, uint16_t, uint8_t*, uint16_t)> callback);
    bool sendTcpDataToClient(const uint8_t* remote_ip, uint16_t remote_port, uint8_t link_no, const uint8_t* data, uint16_t length);
    bool closeTcpConnection(const uint8_t* remote_ip, uint16_t remote_port, uint8_t link_no);
    int gets(char** buf, int size = 0);
    int puts(const char*, int size = 0);
    int putc(int c);
    int getc(void);
    bool ready();
    bool has_char(char letter);
    int type(); // 0: serial, 1: wifi


private:
    void set_wifi_op_mode(u8 op_mode);

    void M8266WIFI_Module_Hardware_Reset(void);
    u8 M8266WIFI_Module_Init_Via_SPI();

    void init_wifi_module(bool reset);
    void query_wifi_status();

    uint32_t ip_to_int(char* ip_addr);
    void int_to_ip(uint32_t i_ip, char *ip_addr);
    void get_broadcast_from_ip_and_netmask(char *broadcast_addr, char *ip_addr, char *netmask);

    void on_pin_rise();
    void receive_wifi_data();

    void halt();

    uint8_t getNextLinkNo();

    uint8_t next_available_link_no;

    mbed::InterruptIn *wifi_interrupt_pin; // Interrupt pin for measuring speed
    float probe_slow_rate;

    RingBuffer<char, 256> buffer; // Receive buffer
    string test_buffer;

    u8 txData[WIFI_DATA_MAX_SIZE];
    u8 rxData[WIFI_DATA_MAX_SIZE];

    std::map<u8, std::function<void(u8*, u16, u8*, u16)>> data_callbacks;

	int tcp_port;
	int udp_send_port;
	int udp_recv_port;
	int tcp_timeout_s;
	int connection_fail_count;
	string machine_name;
	char ap_address[16];
	char ap_netmask[16];
	char sta_address[16];
	char sta_netmask[16];

    struct {
    	u8  tcp_link_no;
    	u8  udp_link_no;
    	bool wifi_init_ok:1;
    	volatile bool has_data_flag:1;
    };

};

#endif /* WIFIPROVIDER_H_ */
