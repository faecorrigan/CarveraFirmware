#ifndef XMODEM_H
#define XMODEM_H

#include <string>
#include <cstdint>
#include "StreamOutput.h"

#include "quicklz.h"

class XModem {
public:
    bool upload(const std::string& filename, StreamOutput* stream);
    bool download(const std::string& filename, StreamOutput* stream);

private:
    // Buffers
    unsigned char xbuff[8200];
    unsigned char lzbuff[DCOMPRESS_BUFFER_SIZE];
    char info_msg[64];

    // Constants
    static const unsigned char SOH = 0x01;
    static const unsigned char STX = 0x02;
    static const unsigned char EOT = 0x04;
    static const unsigned char ACK = 0x06;
    static const unsigned char NAK = 0x15;
    static const unsigned char CAN = 0x16; // FIXME, should be 0x18 but Carvera seems to use 0x16 (SYN) instead
    static const unsigned char CTRLZ = 0x1A;

    // Helper Functions
    int inbyte(unsigned int timeout_ms, StreamOutput* stream);
    int inbytes(char** buf, int size, unsigned int timeout_ms, StreamOutput* stream);
    void flush_input(StreamOutput* stream);
    void cancel_transfer(StreamOutput* stream);
    void set_serial_rx_irq(bool enable);

    // CRC Functions
    unsigned int crc16_ccitt(unsigned char* data, unsigned int len);
    int check_crc(int crc, unsigned char* data, unsigned int len);

    // Additional Helpers
    bool decompress(const std::string& sfilename, const std::string& dfilename, uint32_t sfilesize, StreamOutput* stream);
    
    // Constants for transfer
    static const int MAXRETRANS = 10;
    static const int TIMEOUT_MS = 100;
};

#endif // XMODEM_H
