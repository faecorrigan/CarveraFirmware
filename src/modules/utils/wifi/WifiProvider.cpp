/*
 * WifiProvider.cpp
 *
 *  Created on: June 10, 2020
 *      Author: Josh
 */

#include "WifiProvider.h"

#include "brd_cfg.h"
#include "M8266HostIf.h"

#include "libs/Module.h"
#include "libs/Kernel.h"
#include "SlowTicker.h"
#include "PublicDataRequest.h"
#include "Config.h"
#include "ConfigValue.h"
#include "checksumm.h"
#include "PublicData.h"
#include "Gcode.h"
#include "libs/Logging.h"
#include "libs/StreamOutput.h"
#include "SwitchPublicAccess.h"
#include "WifiPublicAccess.h"
#include "libs/utils.h"
#include "Logging.h"

#include "libs/SerialMessage.h"
#include "libs/StreamOutput.h"

#include "port_api.h"
#include "InterruptIn.h"

#include "gpio.h"

#include <math.h>

#define wifi_checksum                   CHECKSUM("wifi")
#define wifi_enable                     CHECKSUM("enable")
#define wifi_interrupt_pin_checksum     CHECKSUM("interrupt_pin")
#define machine_name_checksum           CHECKSUM("machine_name")
#define tcp_port_checksum               CHECKSUM("tcp_port")
#define udp_send_port_checksum          CHECKSUM("udp_send_port")
#define udp_recv_port_checksum          CHECKSUM("udp_recv_port")
#define tcp_timeout_s_checksum          CHECKSUM("tcp_timeout_s")

void WifiProvider::init()
{
    udp_link_no = 0;
    tcp_link_no = 1;
    next_available_link_no = 2;
    wifi_init_ok = false;
    has_data_flag = false;
    connection_fail_count = 0;
}

void WifiProvider::on_module_loaded()
{
    // Check if WiFi is enabled in the configuration
    if (!THEKERNEL.config->value(wifi_checksum, wifi_enable)->by_default(true)->as_bool()) {
        // Not needed; free up resources
        return;
    }

	data_callbacks.clear();

    // Load configuration values
    this->tcp_port = THEKERNEL.config->value(wifi_checksum, tcp_port_checksum)->by_default(2222)->as_int();
    this->udp_send_port = THEKERNEL.config->value(wifi_checksum, udp_send_port_checksum)->by_default(3333)->as_int();
    this->udp_recv_port = THEKERNEL.config->value(wifi_checksum, udp_recv_port_checksum)->by_default(4444)->as_int();
    this->tcp_timeout_s = THEKERNEL.config->value(wifi_checksum, tcp_timeout_s_checksum)->by_default(10)->as_int();
    this->machine_name = THEKERNEL.config->value(wifi_checksum, machine_name_checksum)->by_default("CARVERA")->as_string();

    // Initialize WiFi module
    this->init_wifi_module(false);

    // Set up interrupt for WiFi data reception
    Pin* smoothie_pin = new Pin();
    smoothie_pin->from_string(THEKERNEL.config->value(wifi_checksum, wifi_interrupt_pin_checksum)->by_default("2.11")->as_string());
    smoothie_pin->as_input();
    if (smoothie_pin->port_number == 0 || smoothie_pin->port_number == 2) {
        PinName pinname = port_pin((PortName)smoothie_pin->port_number, smoothie_pin->pin);
        wifi_interrupt_pin = new mbed::InterruptIn(pinname);
        wifi_interrupt_pin->rise(this, &WifiProvider::on_pin_rise);
        NVIC_SetPriority(EINT3_IRQn, 16);
    } else {
        printk("Error: WiFi interrupt pin must be on P0 or P2.\n");
        return;
    }
    delete smoothie_pin;

    // Add this stream to the kernel's stream pool for broadcasting
    THEKERNEL.streams.append_stream(this);

    // Register for events
    this->register_for_event(ON_IDLE);
    this->register_for_event(ON_GCODE_RECEIVED);
    this->register_for_event(ON_MAIN_LOOP);
    this->register_for_event(ON_SECOND_TICK);
    this->register_for_event(ON_GET_PUBLIC_DATA);
    this->register_for_event(ON_SET_PUBLIC_DATA);
}

void WifiProvider::on_pin_rise()
{
    has_data_flag = true;
}

void WifiProvider::receive_wifi_data()
{
    u8 link_no;
    u16 received = 0;
    u16 status;
	u8 remote_ip[4];
	u16 remote_port;

    while (true) {
        received = M8266WIFI_SPI_RecvData_ex(rxData, WIFI_DATA_MAX_SIZE, WIFI_DATA_TIMEOUT_MS, &link_no, remote_ip, &remote_port, &status);

		// Check if there is a callback registered for this link
        auto it = data_callbacks.find(link_no);
        if (it != data_callbacks.end()) {
            // Call the registered callback function
            it->second(remote_ip, remote_port, rxData, received);
        } else {
			if (link_no == udp_link_no) {
				// Ignore UDP data
				return;
			}

			if (link_no == tcp_link_no) {
				// Data received from the primary TCP connection
				for (int i = 0; i < received; i++) {
					// Handle special control characters
					if (rxData[i] == '?') {
                        puts(THEKERNEL.get_query_string().c_str());
						continue;
					}
					if (rxData[i] == '*') {
                        puts(THEKERNEL.get_diagnose_string().c_str(), 0);
						continue;
					}
					if (rxData[i] == 'X' - 'A' + 1) { // Ctrl+X
						halt();
						continue;
					}
					if (THEKERNEL.is_feed_hold_enabled()) {
						if (rxData[i] == '!') { // Safe pause
							THEKERNEL.set_feed_hold(true);
							continue;
						}
						if (rxData[i] == '~') { // Safe resume
							THEKERNEL.set_feed_hold(false);
							continue;
						}
					}
					// Convert carriage return to newline
					if (rxData[i] == '\r') {
						rxData[i] = '\n';
					}
					// Add data to buffer
					this->buffer.push_back(char(rxData[i]));
				}
			}
		}

		if (received < WIFI_DATA_MAX_SIZE) {
			return;
		}
    }
}

bool WifiProvider::ready()
{
    return M8266WIFI_SPI_Has_DataReceived();
}

void WifiProvider::get_broadcast_from_ip_and_netmask(char* broadcast_addr, char* ip_addr, char* netmask)
{
    uint32_t i_ip = ip_to_int(ip_addr);
    uint32_t i_mask = ip_to_int(netmask);
    uint32_t i_broadcast = i_ip | (~i_mask);
    int_to_ip(i_broadcast, broadcast_addr);
}

void WifiProvider::int_to_ip(uint32_t i_ip, char* ip_addr)
{
    unsigned int bytes[4];
    bytes[0] = (i_ip >> 24) & 0xFF;
    bytes[1] = (i_ip >> 16) & 0xFF;
    bytes[2] = (i_ip >> 8) & 0xFF;
    bytes[3] = i_ip & 0xFF;
    snprintf(ip_addr, 16, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
}

uint32_t WifiProvider::ip_to_int(char* ip_addr)
{
    unsigned int bytes[4];
    sscanf(ip_addr, "%u.%u.%u.%u", &bytes[0], &bytes[1], &bytes[2], &bytes[3]);
    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

void WifiProvider::on_second_tick(void*)
{
    u16 status = 0;
    char address[16];
    char udp_buff[100];
    u8 param_len = 0;
    u8 connection_status = 0;
    u8 client_num = 0;
    ClientInfo RemoteClients[15];

    if (!wifi_init_ok || THEKERNEL.is_uploading()) return;

    // List clients connected to TCP server
    M8266WIFI_SPI_List_Clients_On_A_TCP_Server(tcp_link_no, &client_num, RemoteClients, &status);

    // Get STA connection status
    M8266WIFI_SPI_Get_STA_Connection_Status(&connection_status, &status);

    if (connection_status == 5) {
        // Connected to AP
        // Get IP and netmask
        M8266WIFI_SPI_Query_STA_Param(STA_PARAM_TYPE_IP_ADDR, (u8*)this->sta_address, &param_len, &status);
        M8266WIFI_SPI_Query_STA_Param(STA_PARAM_TYPE_NETMASK_ADDR, (u8*)this->sta_netmask, &param_len, &status);
        // Calculate broadcast address and send UDP data
        get_broadcast_from_ip_and_netmask(address, this->sta_address, this->sta_netmask);
        snprintf(udp_buff, sizeof(udp_buff), "%s,%s,%d,%d", this->machine_name.c_str(), this->sta_address, this->tcp_port, client_num > 0 ? 1 : 0);
        M8266WIFI_SPI_Send_Udp_Data((u8*)udp_buff, strlen(udp_buff), udp_link_no, address, this->udp_send_port, &status);
        connection_fail_count = 0;
    } else if (connection_status == 2 || connection_status == 3 || connection_status == 4) {
        // Connection failed
        connection_fail_count++;
        if (connection_fail_count > 10) {
            // Disconnect WiFi
            if (M8266WIFI_SPI_STA_DisConnect_Ap(&status)) {
                printk("STA connection timeout, disconnected!\n");
            }
            connection_fail_count = 0;
        }
    } else {
        connection_fail_count = 0;
    }

    // Send AP info through UDP
    memset(udp_buff, 0, sizeof(udp_buff));
    get_broadcast_from_ip_and_netmask(address, this->ap_address, this->ap_netmask);
    snprintf(udp_buff, sizeof(udp_buff), "%s,%s,%d,%d", this->machine_name.c_str(), this->ap_address, this->tcp_port, client_num > 0 ? 1 : 0);
    M8266WIFI_SPI_Send_Udp_Data((u8*)udp_buff, strlen(udp_buff), udp_link_no, address, this->udp_send_port, &status);
}

void WifiProvider::on_idle(void* argument)
{
    if (THEKERNEL.is_uploading()) return;

    // Check for incoming data
    if (has_data_flag || M8266WIFI_SPI_Has_DataReceived()) {
        has_data_flag = false;
        receive_wifi_data();
    }
}

void WifiProvider::halt(void) {
    THEKERNEL.call_event(ON_HALT, nullptr);
    THEKERNEL.set_halt_reason(MANUAL);
    if (THEKERNEL.is_grbl_mode()) {
        puts("ALARM: Abort during cycle\r\n");
    } else {
        puts("HALTED, M999 or $X to exit HALT state\r\n");
    }
}

void WifiProvider::on_main_loop(void* argument)
{
    // Process buffered input when a newline character is found
    if (this->has_char('\n')) {
        string received;
        received.reserve(20);
        while (1) {
            char c;
            this->buffer.pop_front(c);
            if (c == '\n') {
                struct SerialMessage message;
                message.message = received;
                message.stream = this;
                THEKERNEL.call_event(ON_CONSOLE_LINE_RECEIVED, &message);
                break;
            } else {
                received += c;
            }
        }
    }
}

int WifiProvider::puts(const char* s, int size)
{
    size_t total_length = size == 0 ? strlen(s) : size;
    size_t sent_index = 0;
    u16 status = 0;
    u32 sent = 0;
    u32 to_send = 0;

    while (sent_index < total_length) {
        // Determine the size of data to send in this chunk
        to_send = std::min(static_cast<u32>(total_length - sent_index), static_cast<u32>(WIFI_DATA_MAX_SIZE));
        memcpy(txData, s + sent_index, to_send);

        // Send data directly from the buffer
        sent = M8266WIFI_SPI_Send_BlockData(
            txData,
            to_send,
            5000,
            tcp_link_no,
            NULL,
            0,
            &status
        );

        sent_index += sent;

        if (sent != to_send) {
            // Error or connection closed
            break;
        }
    }

    return sent_index;
}

int WifiProvider::putc(int c)
{
    u16 status = 0;
    u8 to_send = c;
    if (M8266WIFI_SPI_Send_Data(&to_send, 1, tcp_link_no, &status) == 0) {
        return 0;
    } else {
        return 1;
    }
}

int WifiProvider::getc()
{
    u16 status;
    u8 to_recv = 0, link_no;
    M8266WIFI_SPI_RecvData(&to_recv, 1, WIFI_DATA_TIMEOUT_MS, &link_no, &status);
    return to_recv;
}

int WifiProvider::gets(char** buf, int size)
{
    u16 status;
    u8 link_no;
    u16 received = M8266WIFI_SPI_RecvData(rxData,
                                          (size == 0 || size > WIFI_DATA_MAX_SIZE) ? WIFI_DATA_MAX_SIZE : size,
                                          WIFI_DATA_TIMEOUT_MS, &link_no, &status);
    if (link_no == udp_link_no) {
        // Ignore UDP data
        return 0;
    }
    *buf = (char*)&rxData;
    return received;
}

bool WifiProvider::has_char(char letter)
{
    int index = this->buffer.tail;
    while (index != this->buffer.head) {
        if (this->buffer.buffer[index] == letter) {
            return true;
        }
        index = this->buffer.next_block_index(index);
    }
    return false;
}

void WifiProvider::on_gcode_received(void* argument)
{
    Gcode* gcode = static_cast<Gcode*>(argument);
    if (gcode->has_m) {
        if (gcode->m == 481) {
            // Basic WiFi operations
            if (gcode->subcode == 1) {
                // Reset WiFi module
                wifi_init_ok = false;
                init_wifi_module(true);
            } else if (gcode->subcode == 2) {
                // Set operation mode to STA+AP
                set_wifi_op_mode(3);
            } else if (gcode->subcode == 3) {
                // Connect to AP
                u8 connection_state;
                printk("M8266WIFI_SPI_Query_Connection...\n");
                if (M8266WIFI_SPI_Query_Connection(tcp_link_no, NULL, &connection_state, NULL, NULL, NULL, NULL) == 0) {
                    printk("M8266WIFI_SPI_Query_Connection ERROR!\n");
                } else {
                    printk("connection_state : %d\n", connection_state);
                }
            } else if (gcode->subcode == 4) {
                // Test data reception
                gcode->stream->printf("M8266WIFI_SPI_Has_DataReceived...\n");
                if (M8266WIFI_SPI_Has_DataReceived()) {
                    gcode->stream->printf("Data Received, receive_wifi_data...\n");
                    gcode->stream->printf("Data Received complete!\n");
                }
            } else if (gcode->subcode == 6) {
                // Test broadcast address calculation
                char ip_addr[16] = "192.168.1.2";
                char netmask[16] = "255.255.255.0";
                char broadcast[16];
                get_broadcast_from_ip_and_netmask(broadcast, ip_addr, netmask);
                gcode->stream->printf("broadcast: %s\n", broadcast);
            }
        } else if (gcode->m == 482) {
            // Query STA parameters
            u16 status = 0;
            char param[64];
            u8 param_len = 0;
            memset(param, 0, sizeof(param));
            STA_PARAM_TYPE param_type;
            switch (gcode->subcode) {
                case 0: param_type = STA_PARAM_TYPE_SSID; break;
                case 1: param_type = STA_PARAM_TYPE_PASSWORD; break;
                case 2: param_type = STA_PARAM_TYPE_CHANNEL; break;
                case 3: param_type = STA_PARAM_TYPE_HOSTNAME; break;
                case 4: param_type = STA_PARAM_TYPE_MAC; break;
                case 5: param_type = STA_PARAM_TYPE_IP_ADDR; break;
                case 6: param_type = STA_PARAM_TYPE_GATEWAY_ADDR; break;
                case 7: param_type = STA_PARAM_TYPE_NETMASK_ADDR; break;
                default: param_type = STA_PARAM_TYPE_SSID;
            }
            if (M8266WIFI_SPI_Query_STA_Param(param_type, (u8*)param, &param_len, &status) == 0) {
                printk("Query WiFi STA parameters ERROR!\n");
            } else {
                if (param_type == STA_PARAM_TYPE_CHANNEL) {
                    printk("STA param[%d]: %d\n", gcode->subcode, *param);
                } else if (param_type == STA_PARAM_TYPE_MAC) {
                    printk("STA param[%d]: %d\n", gcode->subcode, param_len);
                } else {
                    printk("STA param[%d]: %s\n", gcode->subcode, param);
                }
            }
        } else if (gcode->m == 483) {
            // Query AP parameters
            u16 status = 0;
            char param[64];
            u8 param_len = 0;
            memset(param, 0, sizeof(param));
            AP_PARAM_TYPE param_type;
            switch (gcode->subcode) {
                case 0: param_type = AP_PARAM_TYPE_SSID; break;
                case 1: param_type = AP_PARAM_TYPE_PASSWORD; break;
                case 2: param_type = AP_PARAM_TYPE_CHANNEL; break;
                case 3: param_type = AP_PARAM_TYPE_AUTHMODE; break;
                case 4: param_type = AP_PARAM_TYPE_IP_ADDR; break;
                case 5: param_type = AP_PARAM_TYPE_GATEWAY_ADDR; break;
                case 6: param_type = AP_PARAM_TYPE_NETMASK_ADDR; break;
                case 7: param_type = AP_PARAM_TYPE_PHY_MODE; break;
                default: param_type = AP_PARAM_TYPE_SSID;
            }
            if (M8266WIFI_SPI_Query_AP_Param(param_type, (u8*)param, &param_len, &status) == 0) {
                printk("Query WiFi AP parameters ERROR!\n");
            } else {
                if (param_type == AP_PARAM_TYPE_CHANNEL || param_type == AP_PARAM_TYPE_AUTHMODE || param_type == AP_PARAM_TYPE_PHY_MODE) {
                    printk("AP param[%d]: %d\n", gcode->subcode, *param);
                } else {
                    printk("AP param[%d]: %s\n", gcode->subcode, param);
                }
            }
        } else if (gcode->m == 489) {
            // Query WiFi status
            query_wifi_status();
        }
    }
}

void WifiProvider::set_wifi_op_mode(u8 op_mode)
{
    u16 status = 0;
    if (M8266WIFI_SPI_Set_Opmode(op_mode, 1, &status) == 0) {
        printk("M8266WIFI_SPI_Set_Opmode, ERROR, status: %d!\n", status);
    } else if (op_mode == 1) {
        printk("WiFi Access Point Disabled...\n");
    } else if (op_mode == 3) {
        printk("WiFi Access Point Enabled...\n");
    }
}

void WifiProvider::on_get_public_data(void* argument)
{
    PublicDataRequest* pdr = static_cast<PublicDataRequest*>(argument);
    if (!pdr->starts_with(wlan_checksum)) return;
    if (!pdr->second_element_is(get_wlan_checksum)) return;

    u8 signals = 0;
    u16 status = 0;
    char ssid[32];
    u8 ssid_len = 0;
    u8 connection_status = 0;

    // Get current connected SSID
    M8266WIFI_SPI_Query_STA_Param(STA_PARAM_TYPE_SSID, (u8*)ssid, &ssid_len, &status);
    M8266WIFI_SPI_Get_STA_Connection_Status(&connection_status, &status);

    ScannedSigs wlans[MAX_WLAN_SIGNALS];
    // Start scanning for WLAN signals
    M8266WIFI_SPI_STA_Scan_Signals(wlans, MAX_WLAN_SIGNALS, 0xff, 0, &status);
    // Wait for scan to finish
    while (true) {
        signals = M8266WIFI_SPI_STA_Fetch_Last_Scanned_Signals(wlans, MAX_WLAN_SIGNALS, &status);
        if (signals == 0) {
            if ((status & 0xff) == 0x26) {
                // Still scanning; wait
                THEKERNEL.call_event(ON_IDLE, this);
                M8266WIFI_Module_delay_ms(1);
                continue;
            } else {
                // Scan failed
                return;
            }
        } else {
            // Prepare data for public request
            size_t n;
            std::string str;
            std::string ssid_str;
            char buf[10];
            for (int i = 0; i < signals; i++) {
                ssid_str = "";
                for (size_t j = 0; j < strlen(wlans[i].ssid); j++) {
                    ssid_str += wlans[i].ssid[j] == ' ' ? 0x01 : wlans[i].ssid[j];
                }
                ssid_str.append(",");
                // Ignore duplicate SSIDs
                if (str.find(ssid_str) != std::string::npos) {
                    continue;
                }
                str.append(ssid_str);
                str.append(wlans[i].authmode == 0 ? "0" : "1");
                str.append(",");
                n = snprintf(buf, sizeof(buf), "%d", wlans[i].rssi);
                if (n > sizeof(buf)) n = sizeof(buf);
                str.append(buf, n);
                str.append(",");
                if (strncmp(ssid, wlans[i].ssid, ssid_len <= 32 ? ssid_len : 32) == 0 && connection_status == 5) {
                    str.append("1\n");
                } else {
                    str.append("0\n");
                }
            }
            // Return data to requester
            char* temp_buf = (char*)malloc(str.length() + 1);
            memcpy(temp_buf, str.c_str(), str.length());
            temp_buf[str.length()] = '\0';
            pdr->set_data_ptr(temp_buf);
            pdr->set_taken();
            return;
        }
    }
}

void WifiProvider::on_set_public_data(void* argument)
{
    PublicDataRequest* pdr = static_cast<PublicDataRequest*>(argument);
    if (!pdr->starts_with(wlan_checksum)) return;
    if (!pdr->second_element_is(set_wlan_checksum)
        && !pdr->second_element_is(ap_set_channel_checksum)
        && !pdr->second_element_is(ap_set_ssid_checksum)
        && !pdr->second_element_is(ap_set_password_checksum)
        && !pdr->second_element_is(ap_enable_checksum)) return;

    if (pdr->second_element_is(set_wlan_checksum)) {
        // Set WLAN connection
        ap_conn_info* s = static_cast<ap_conn_info*>(pdr->get_data_ptr());
        u16 status = 0;
        u8 connection_status;

        s->has_error = false;
        if (s->disconnect) {
            // Disconnect from AP
            if (M8266WIFI_SPI_STA_DisConnect_Ap(&status) == 0) {
                s->has_error = true;
                snprintf(s->error_info, sizeof(s->error_info), "Disconnect error!");
            }
        } else {
            // Connect to AP
            M8266WIFI_SPI_STA_Connect_Ap((u8*)s->ssid, (u8*)s->password, 1, 0, &status);
            // Wait for connection
            while (true) {
                M8266WIFI_SPI_Get_STA_Connection_Status(&connection_status, &status);
                if (connection_status == 1) {
                    // Connecting; wait
                    THEKERNEL.call_event(ON_IDLE, this);
                    M8266WIFI_Module_delay_ms(1);
                    continue;
                } else if (connection_status == 5) {
                    // Connection successful
                    s->has_error = false;
                    break;
                } else {
                    // Connection failed
                    s->has_error = true;
                    if (connection_status == 0) {
                        snprintf(s->error_info, sizeof(s->error_info), "No connection started!");
                    } else if (connection_status == 2) {
                        snprintf(s->error_info, sizeof(s->error_info), "WiFi password incorrect!");
                    } else if (connection_status == 3) {
                        snprintf(s->error_info, sizeof(s->error_info), "WiFi SSID not found: %s!", s->ssid);
                    } else if (connection_status == 4) {
                        snprintf(s->error_info, sizeof(s->error_info), "Other error!");
                    }
                    break;
                }
            }
            // Get IP address if connected
            if (!s->has_error) {
                M8266WIFI_SPI_Get_STA_IP_Addr(s->ip_address, &status);
            }
        }
    } else if (pdr->second_element_is(ap_set_channel_checksum)) {
        // Set AP channel
        u16 status = 0;
        u8 ap_channel = *static_cast<u8*>(pdr->get_data_ptr());
        if (M8266WIFI_SPI_Config_AP_Param(AP_PARAM_TYPE_CHANNEL, &ap_channel, 1, 1, &status) == 0) {
            printk("WiFi set AP Channel ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
        } else {
            printk("WiFi AP Channel changed to %d\n", ap_channel);
        }
    } else if (pdr->second_element_is(ap_set_ssid_checksum)) {
        // Set AP SSID
        u16 status = 0;
        char* ssid = static_cast<char*>(pdr->get_data_ptr());
        if (M8266WIFI_SPI_Config_AP_Param(AP_PARAM_TYPE_SSID, (u8*)ssid, strlen(ssid), 1, &status) == 0) {
            printk("WiFi set AP SSID ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
        } else {
            printk("WiFi AP SSID changed to %s\n", ssid);
        }
    } else if (pdr->second_element_is(ap_set_password_checksum)) {
        // Set AP password
        u16 status = 0;
        u8 op_mode;
        // Ensure module is in AP mode
        if (M8266WIFI_SPI_Get_Opmode(&op_mode, &status) == 0) {
            printk("WiFi get OP mode ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
        } else {
            if (op_mode != 3) {
                printk("WiFi cannot set password when not in AP mode!\n");
            } else {
                char* password = static_cast<char*>(pdr->get_data_ptr());
                u8 authmode = strlen(password) == 0 ? 0 : 4;
                if (M8266WIFI_SPI_Config_AP_Param(AP_PARAM_TYPE_PASSWORD, (u8*)password, strlen(password), 1, &status) > 0) {
                    printk("WiFi AP Password changed to %s\n", password);
                }
                if (M8266WIFI_SPI_Config_AP_Param(AP_PARAM_TYPE_AUTHMODE, &authmode, 1, 1, &status) == 0) {
                    // Do nothing
                }
            }
        }
    } else if (pdr->second_element_is(ap_enable_checksum)) {
        // Enable or disable AP mode
        bool* enable_op = static_cast<bool*>(pdr->get_data_ptr());
        if (*enable_op) {
            set_wifi_op_mode(3);
        } else {
            set_wifi_op_mode(1);
        }
    }
    pdr->set_taken();
}

void WifiProvider::query_wifi_status()
{
    u16 status = 0;
    u32 esp8266_id;
    u8 flash_size;
    char fw_ver[24] = "";
    printk("M8266WIFI_SPI_Get_Module_Info...\n");
    if (M8266WIFI_SPI_Get_Module_Info(&esp8266_id, &flash_size, fw_ver, &status) == 0) {
        printk("M8266WIFI_SPI_Get_Module_Info ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    } else {
        printk("esp8266_id:%ld, flash_size:%d, fw_ver:%s!\n", esp8266_id, flash_size, fw_ver);
    }
}

void WifiProvider::init_wifi_module(bool reset)
{
    u16 status = 0;
    char address[16];
    u8 param_len = 0;

    if (reset) {
        // Reset module: delete connections and remove stream
        M8266WIFI_SPI_Delete_Connection(udp_link_no, &status);
        M8266WIFI_SPI_Delete_Connection(tcp_link_no, &status);
        THEKERNEL.streams.remove_stream(this);
    }

    // Initialize module via SPI
    M8266HostIf_Init();
    if (M8266WIFI_Module_Init_Via_SPI() == 0) {
        printk("M8266WIFI_Module_Init_Via_SPI, ERROR!\n");
    }

    // Set up TCP and UDP connections
    snprintf(address, sizeof(address), "192.168.4.10");
    if (M8266WIFI_SPI_Setup_Connection(2, this->tcp_port, address, 0, tcp_link_no, 3, &status) == 0) {
        printk("M8266WIFI_SPI_Setup_Connection ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    }
    snprintf(address, sizeof(address), "192.168.4.255");
    if (M8266WIFI_SPI_Setup_Connection(0, this->udp_recv_port, address, 0, udp_link_no, 3, &status) == 0) {
        printk("M8266WIFI_SPI_Setup_Connection ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    }

    // Set TCP server auto-disconnect timeout
    if (M8266WIFI_SPI_Set_TcpServer_Auto_Discon_Timeout(tcp_link_no, tcp_timeout_s, &status) == 0) {
        printk("M8266WIFI_SPI_Set_TcpServer_Auto_Discon_Timeout ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    }

    // Load current AP IP and Netmask
    if (M8266WIFI_SPI_Query_AP_Param(AP_PARAM_TYPE_IP_ADDR, (u8*)this->ap_address, &param_len, &status) == 0) {
        printk("Get AP_PARAM_TYPE_IP_ADDR ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    }
    if (M8266WIFI_SPI_Query_AP_Param(AP_PARAM_TYPE_NETMASK_ADDR, (u8*)this->ap_netmask, &param_len, &status) == 0) {
        printk("Get AP_PARAM_TYPE_NETMASK_ADDR ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
    }

    if (reset) {
        // Re-append stream after reset
        THEKERNEL.streams.append_stream(this);
    }

    wifi_init_ok = true;
}

void WifiProvider::M8266WIFI_Module_delay_ms(u16 nms)
{
    // Delay function for module initialization
    u16 i, j;
    for (i = 0; i < nms; i++)
        for (j = 0; j < 4; j++)
            M8266HostIf_delay_us(250);
}

void WifiProvider::M8266WIFI_Module_Hardware_Reset(void) // total 800ms  (Chinese: 本例子中这个函数的总共执行时间大约800毫秒)
{
	M8266HostIf_Set_SPI_nCS_Pin(0);   			// Module nCS==ESP8266 GPIO15 as well, Low during reset in order for a normal reset (Chinese: 为了实现正常复位，模块的片选信号nCS在复位期间需要保持拉低)
	M8266WIFI_Module_delay_ms(1); 	    		// delay 1ms, adequate for nCS stable (Chinese: 延迟1毫秒，确保片选nCS设置后有足够的时间来稳定)

	M8266HostIf_Set_nRESET_Pin(0);					// Pull low the nReset Pin to bring the module into reset state (Chinese: 拉低nReset管脚让模组进入复位状态)
	M8266WIFI_Module_delay_ms(5);      		// delay 5ms, adequate for nRESET stable(Chinese: 延迟5毫秒，确保片选nRESER设置后有足够的时间来稳定，也确保nCS和nRESET有足够的时间同时处于低电平状态)
	                                        // give more time especially for some board not good enough
	                                        //(Chinese: 如果主板不是很好，导致上升下降过渡时间较长，或者因为失配存在较长的振荡时间，所以信号到轨稳定的时间较长，那么在这里可以多给一些延时)

	M8266HostIf_Set_nRESET_Pin(1);					// Pull high again the nReset Pin to bring the module exiting reset state (Chinese: 拉高nReset管脚让模组退出复位状态)
	M8266WIFI_Module_delay_ms(300); 	  		// at least 18ms required for reset-out-boot sampling boottrap pin (Chinese: 至少需要18ms的延时来确保退出复位时足够的boottrap管脚采样时间)
	                                        // Here, we use 300ms for adequate abundance, since some board GPIO, (Chinese: 在这里我们使用了300ms的延时来确保足够的富裕量，这是因为在某些主板上，)
																					// needs more time for stable(especially for nRESET) (Chinese: 他们的GPIO可能需要较多的时间来输出稳定，特别是对于nRESET所对应的GPIO输出)
																					// You may shorten the time or give more time here according your board v.s. effiency
																					// (Chinese: 如果你的主机板在这里足够好，你可以缩短这里的延时来缩短复位周期；反之则需要加长这里的延时。
																					//           总之，你可以调整这里的时间在你们的主机板上充分测试，找到一个合适的延时，确保每次复位都能成功。并适当保持一些富裕量，来兼容批量化时主板的个体性差异)
	M8266HostIf_Set_SPI_nCS_Pin(1);         // release/pull-high(defualt) nCS upon reset completed (Chinese: 释放/拉高(缺省)片选信号
	//M8266WIFI_Module_delay_ms(1); 	    		// delay 1ms, adequate for nCS stable (Chinese: 延迟1毫秒，确保片选nCS设置后有足够的时间来稳定)

	M8266WIFI_Module_delay_ms(800-300-5-2); // Delay more than around 500ms for M8266WIFI module bootup and initialization，including bootup information print。No influence to host interface communication. Could be shorten upon necessary. But test for verification required if adjusted.
	                                        // (Chinese: 延迟大约500毫秒，来等待模组成功复位后完成自己的启动过程和自身初始化，包括串口信息打印。但是此时不影响模组和单片主机之间的通信，这里的时间可以根据需要适当调整.如果调整缩短了这里的时间，建议充分测试，以确保系统(时序关系上的)可靠性)
}

u8 WifiProvider::M8266WIFI_Module_Init_Via_SPI()
{
    u16 status = 0;
    uint32_t spi_clk = 24000000;

    // Step 1: Hardware reset the module
    M8266WIFI_Module_Hardware_Reset();

    // Step 2: Set SPI clock speed
	#ifndef SPI_BaudRatePrescaler_2
	#define SPI_BaudRatePrescaler_2         ((u32)0x00000002U)
	#define SPI_BaudRatePrescaler_4         ((u32)0x00000004U)
	#define SPI_BaudRatePrescaler_6         ((u32)0x00000006U)
	#define SPI_BaudRatePrescaler_8         ((u32)0x00000008U)
	#define SPI_BaudRatePrescaler_16        ((u32)0x00000010U)
	#define SPI_BaudRatePrescaler_32        ((u32)0x00000020U)
	#define SPI_BaudRatePrescaler_64        ((u32)0x00000040U)
	#define SPI_BaudRatePrescaler_128       ((u32)0x00000080U)
	#define SPI_BaudRatePrescaler_256       ((u32)0x00000100U)
	#endif	
    M8266HostIf_SPI_SetSpeed(SPI_BaudRatePrescaler_4);
    spi_clk = 24000000;
    M8266WIFI_Module_delay_ms(1);

    // Step 3: Select SPI interface
    if (M8266HostIf_SPI_Select((uint32_t)M8266WIFI_INTERFACE_SPI, spi_clk, &status) == 0) {
        printk("M8266HostIf_SPI_Select ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
        return 0;
    }

    // Step 4: Communication test
    u8 byte;
    if (M8266WIFI_SPI_Interface_Communication_OK(&byte) == 0) {
        printk("Communication test ERROR!\n");
        return 0;
    }

	int i = 100000;
	int j = M8266WIFI_SPI_Interface_Communication_Stress_Test(i);
	if( (j < i) && (i - j > 5)) 		//  if SPI Communication stress test failed (Chinese: SPI底层通信压力测试失败，表明你的主机板或接线支持不了当前这么高的SPI频率设置)
	{
		printk("Wifi Module Stress test ERROR!\n");
		return 0;
	}

    // Step 5: Configure module
    if (M8266WIFI_SPI_Set_Tx_Max_Power(68, &status) == 0) {
        printk("M8266WIFI_SPI_Set_Tx_Max_Power ERROR, status:%d, high: %d, low: %d!\n", status, int(status >> 8), int(status & 0xff));
        return 0;
    }

    return 1;
}

int WifiProvider::type()
{
    return 1;
}


/* API */

uint8_t WifiProvider::getNextLinkNo() {
    return next_available_link_no++;
}

uint8_t WifiProvider::initializeTcpServer(uint16_t local_port, uint8_t max_clients)
{
    uint16_t status = 0;
    const int connection_type = 2; // TCP Server
    const int timeout = 3;
    uint8_t link_no = getNextLinkNo();

    // Setup the connection
    if (M8266WIFI_SPI_Setup_Connection(connection_type, local_port, const_cast<char*>("0.0.0.0"), 0, link_no, timeout, &status) == 0) {
        printk("Setup_Connection ERROR on link %d, status: %d\n", link_no, status);
        return 0xFF;
    }

    // Configure the maximum number of clients allowed for a TCP server
    if (connection_type == 2) {
        if (M8266WIFI_SPI_Config_Max_Clients_Allowed_To_A_Tcp_Server(link_no, max_clients, &status) == 0) {
            printk("Config_Max_Clients ERROR on link %d, status: %d\n", link_no, status);
            return 0xFF;
        }
    }

    return link_no;
}

void WifiProvider::registerTcpDataCallback(uint8_t link_no, std::function<void(uint8_t *, uint16_t, uint8_t*, uint16_t)> callback)
{
    data_callbacks[link_no] = callback;
}

bool WifiProvider::sendTcpDataToClient(const uint8_t* remote_ip, uint16_t remote_port, uint8_t link_no, const uint8_t* data, uint16_t length)
{
    uint16_t status = 0;
    char ip_str[16];

    // Convert remote_ip (uint8_t[4]) to string format
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3]);

    printk("WifiProvider::sendTcpDataToClient: Starting to send data to %s:%d on link %d, Total length: %d\n", ip_str, remote_port, link_no, length);

    uint32_t sent_index = 0;
    uint16_t sent = 0;
    uint16_t to_send = 0;

    while (sent_index < length) {
        // Determine how much data to send in this chunk
        to_send = std::min(static_cast<uint16_t>(length - sent_index), static_cast<uint16_t>(WIFI_DATA_MAX_SIZE));

        // Debug statement before sending
        printk("WifiProvider::sendTcpDataToClient: Attempting to send %d bytes to %s:%d on link %d, sent_index: %ld, status before sending: %d\n", 
                                    to_send, ip_str, remote_port, link_no, sent_index, status);

        memcpy(txData, data + sent_index, to_send);
        // Send data directly from the original buffer with a cast to `u8*`
        sent = M8266WIFI_SPI_Send_Data_to_TcpClient(
            txData,
            to_send,
            link_no,
            ip_str,
            remote_port,
            &status
        );

        sent_index += sent;

        if (sent != to_send) {
            // Error or connection closed
            printk("WifiProvider::sendTcpDataToClient: ERROR on link %d to %s:%d, sent %d of %d bytes, status: %d\n", link_no, ip_str, remote_port, sent, to_send, status);
            return false;
        }

        // Debug statement after successful send
        printk("WifiProvider::sendTcpDataToClient: Successfully sent %d bytes to %s:%d on link %d, Total sent: %ld/%d\n", sent, ip_str, remote_port, link_no, sent_index, length);
    }

    printk("WifiProvider::sendTcpDataToClient: Completed sending all data to %s:%d on link %d\n", ip_str, remote_port, link_no);
    return true;
}

bool WifiProvider::closeTcpConnection(const uint8_t* remote_ip, uint16_t remote_port, uint8_t link_no)
{
    uint16_t status = 0;
    uint8_t client_num = 0;
    ClientInfo RemoteClients[15]; // Adjust the size based on maximum expected clients

    // Get the list of clients connected to the TCP server
    if (M8266WIFI_SPI_List_Clients_On_A_TCP_Server(link_no, &client_num, RemoteClients, &status) == 0) {
        printk("Failed to list clients on link %d, status:%d\n", link_no, status);
        return false;
    }

    // Iterate through the clients to find the matching one
    for (uint8_t i = 0; i < client_num; ++i) {
        ClientInfo& client = RemoteClients[i];

        // Compare IP addresses and port
        if (memcmp(client.remote_ip, remote_ip, 4) == 0 && client.remote_port == remote_port) {
            // Found the matching client, disconnect it
            if (M8266WIFI_SPI_Disconnect_TcpClient(link_no, &client, &status) == 0) {
                printk("Failed to disconnect client on link %d, status:%d\n", link_no, status);
                return false;
            }
            return true; // Disconnected successfully
        }
    }

    // Client not found
    printk("Client not found on link %d\n", link_no);
    return false;
}
