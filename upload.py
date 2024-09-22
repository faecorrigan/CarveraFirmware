#!/usr/bin/env python3
import argparse
import socket
import sys
import time
import os
import hashlib
from tqdm import tqdm

# Constants for the protocol
SOH = 0x01  # Start of Header for 128-byte packets
STX = 0x02  # Start of Text for 8192-byte packets
EOT = 0x04  # End of Transmission
ACK = 0x06  # Acknowledge
NAK = 0x15  # Negative Acknowledge
CAN = 0x18  # Cancel
CRC_CHR = ord('C')  # ASCII 'C' indicates use of CRC
MAXRETRANS = 20
TIMEOUT = 10  # seconds

PACKET_SIZE_SOH = 128   # For SOH packets
PACKET_SIZE_STX = 8192  # For STX packets

def calc_crc16(data):
    """Calculate the CRC-16-CCITT checksum."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def create_packet(packet_no, data, header=SOH):
    """Create a packet according to the protocol."""
    if header == SOH:
        bufsz = PACKET_SIZE_SOH
        length_field_size = 1
    elif header == STX:
        bufsz = PACKET_SIZE_STX
        length_field_size = 2
    else:
        raise ValueError("Invalid header byte. Use SOH (0x01) or STX (0x02).")
    
    header_byte = header.to_bytes(1, 'big')
    packet_no_byte = packet_no.to_bytes(1, 'big')
    packet_no_complement = (0xFF - packet_no).to_bytes(1, 'big')
    
    # Set length field
    length = len(data)
    if header == SOH:
        length_bytes = length.to_bytes(1, 'big')
    else:  # STX
        length_bytes = length.to_bytes(2, 'big')  # 2-byte length field, big endian
    
    # Pad data to bufsz with 0x1A
    data_padded = data.ljust(bufsz, b'\x1A')
    
    # Calculate CRC over length_bytes + data_padded
    crc_data = length_bytes + data_padded
    crc = calc_crc16(crc_data)
    crc_bytes = crc.to_bytes(2, 'big')
    
    # Construct packet
    packet = header_byte + packet_no_byte + packet_no_complement + length_bytes + data_padded + crc_bytes
    return packet

def read_byte(sock, timeout=TIMEOUT):
    """Read a single byte from the socket with a timeout."""
    sock.settimeout(timeout)
    try:
        data = sock.recv(1)
        if data:
            return data[0]
        else:
            return None
    except socket.timeout:
        return None

def get_ack(sock, timeout=TIMEOUT):
    """Wait for ACK or NAK from the server."""
    c = read_byte(sock, timeout)
    if c == ACK:
        return True
    elif c == NAK:
        return False
    elif c == CAN:
        print('Received CANCEL from server')
        sys.exit(1)
    else:
        return None  # Unexpected character

def flush_input(sock):
    """Flush any incoming data from the socket."""
    sock.settimeout(0)
    try:
        while True:
            if not sock.recv(1):
                break
    except:
        pass
    sock.settimeout(None)

def send_reset_command(sock):
    """Send a reset command to the server and display the response."""
    reset_command = 'reset\n'.encode('utf-8')
    try:
        print('Sending reset command to server...')
        sock.sendall(reset_command)
        # Receive the response
        sock.settimeout(TIMEOUT)
        response = b''
        while True:
            try:
                chunk = sock.recv(1024)
                if not chunk:
                    break
                response += chunk
                # Assuming the response ends with a newline
                if b'\n' in chunk:
                    break
            except socket.timeout:
                break
        response_str = response.decode('utf-8').strip()
        print(f'Received response for reset command: "{response_str}"')
        # Optionally, check if response indicates success
        if 'Rebooting' in response_str:
            return True
        else:
            return False
    except socket.timeout:
        print('Timeout waiting for acknowledgment of reset command.')
        return False
    except Exception as e:
        print(f'Error sending reset command: {e}')
        return False

def read_all_server_messages(sock, timeout=2):
    """Read all remaining messages from the server after upload."""
    sock.settimeout(timeout)
    messages = b''
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            messages += chunk
    except socket.timeout:
        pass
    except Exception as e:
        print(f'Error reading server messages: {e}')
    finally:
        sock.settimeout(None)  # Restore default timeout
    if messages:
        try:
            return messages.decode('utf-8').strip()
        except UnicodeDecodeError:
            return messages.hex()
    return ''

def main():
    # Parse command-line arguments
    parser = argparse.ArgumentParser(description='Upload a file to a server using a TCP line-based protocol.')
    parser.add_argument('host', help='Host to connect to')
    parser.add_argument('port', type=int, help='Port to connect to')
    parser.add_argument('source_file', help='Source file to upload')
    parser.add_argument('destination_path', help='Destination path on server')
    parser.add_argument('-r', '--reset', action='store_true', help='Submit a reset command after upload')
    
    args = parser.parse_args()

    # Validate source file
    if not os.path.isfile(args.source_file):
        print(f'Error: Source file "{args.source_file}" does not exist or is not a file.')
        sys.exit(1)

    # Extract basename from source_file
    basename = os.path.basename(args.source_file)
    print(f'Uploading file: {basename} to destination path: {args.destination_path}')

    # Create a TCP socket and connect to the server
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((args.host, args.port))
        print(f'Connected to {args.host}:{args.port}')
    except Exception as e:
        print(f'Failed to connect to {args.host}:{args.port}: {e}')
        sys.exit(1)

    try:
        # Send 'upload $destination_path\n'
        upload_command = f'upload {args.destination_path}\n'
        sock.sendall(upload_command.encode('utf-8'))
        print(f'Sent upload command: {upload_command.strip()}')

        # Read the file data
        with open(args.source_file, 'rb') as f:
            file_data = f.read()

        file_length = len(file_data)
        print(f'Preparing to upload file: {args.source_file} ({file_length} bytes)')

        # Compute MD5 checksum
        md5sum = hashlib.md5(file_data).hexdigest()
        print(f'MD5 checksum of file: {md5sum}')

        # Wait for 'C' or NAK from the server
        retries = MAXRETRANS
        while retries > 0:
            c = read_byte(sock, timeout=TIMEOUT)
            if c is None:
                retries -= 1
                print(f'No response from server, retrying... ({MAXRETRANS - retries}/{MAXRETRANS})')
                time.sleep(1)
                continue
            if c == CRC_CHR or c == NAK:
                break
            elif c == CAN:
                print('Received CANCEL from server')
                sys.exit(1)
            else:
                retries -= 1
                print(f'Unexpected character received: {c}, retrying... ({MAXRETRANS - retries}/{MAXRETRANS})')
                time.sleep(1)

        if retries == 0:
            print('Timeout waiting for initial response from server')
            sys.exit(1)

        print('Server ready to receive data')

        # Send MD5 checksum packet using STX (packet type 2)
        packet_no = 0
        md5_data = md5sum.encode('ascii')  # 32-byte ASCII string
        md5_packet = create_packet(packet_no, md5_data, header=STX)
        retransmissions = MAXRETRANS

        print('Sending MD5 checksum packet using STX (8192-byte packet)...')
        while retransmissions > 0:
            sock.sendall(md5_packet)
            ack = get_ack(sock)
            if ack is True:
                print('MD5 checksum packet acknowledged by server')
                break
            elif ack is False:
                retransmissions -= 1
                print(f'MD5 checksum packet not acknowledged, retrying... ({MAXRETRANS - retransmissions}/{MAXRETRANS})')
                time.sleep(1)
                continue
            else:
                retransmissions -= 1
                print(f'Unexpected response when sending MD5 checksum packet, retrying... ({MAXRETRANS - retransmissions}/{MAXRETRANS})')
                time.sleep(1)
                continue

        if retransmissions == 0:
            print('Failed to send MD5 checksum packet after maximum retries')
            sys.exit(1)

        # Send data packets with progress bar using SOH (packet type 1)
        packet_no = 1
        index = 0
        print('Starting file upload...')
        with tqdm(total=file_length, unit='B', unit_scale=True, desc='Uploading', ascii=True) as pbar:
            while index < file_length:
                data_chunk = file_data[index:index + PACKET_SIZE_SOH]
                packet = create_packet(packet_no, data_chunk, header=SOH)
                retransmissions = MAXRETRANS
                while retransmissions > 0:
                    sock.sendall(packet)
                    ack = get_ack(sock)
                    if ack is True:
                        break
                    elif ack is False:
                        retransmissions -= 1
                        print(f'Packet {packet_no} not acknowledged, retrying... ({MAXRETRANS - retransmissions}/{MAXRETRANS})')
                        time.sleep(1)
                        continue
                    else:
                        retransmissions -= 1
                        print(f'Unexpected response for packet {packet_no}, retrying... ({MAXRETRANS - retransmissions}/{MAXRETRANS})')
                        time.sleep(1)
                        continue
                if retransmissions == 0:
                    print(f'Failed to send packet {packet_no} after maximum retries')
                    sys.exit(1)
                index += len(data_chunk)
                pbar.update(len(data_chunk))
                packet_no = (packet_no + 1) % 256  # Wrap around at 256

        # Send EOT to signal end of transmission
        sock.sendall(EOT.to_bytes(1, 'big'))
        print('File transmission completed, sending EOT')

        # Wait for ACK after EOT
        ack = get_ack(sock)
        if ack is True:
            print('File upload completed successfully')
        else:
            print('Failed to receive final ACK after EOT')
            sys.exit(1)

        # Consume and display all remaining messages from the server
        print('Consuming and displaying any additional server messages...')
        server_messages = read_all_server_messages(sock)
        if server_messages:
            print('Server messages after upload:')
            print(server_messages)
        else:
            print('No additional messages received from server.')

        # Optionally, send reset command if -r flag is set
        if args.reset:
            print('Reset flag is set. Initiating reset command...')
            # Optionally, flush any remaining input
            flush_input(sock)
            reset_success = send_reset_command(sock)
            if reset_success:
                print('Reset command executed successfully.')
            else:
                print('Reset command failed.')
                sys.exit(1)

    finally:
        # Close the socket
        sock.close()
        print('Connection closed')

if __name__ == '__main__':
    main()
