#!/usr/bin/env python3

import socket
import threading
import sys
import select
from serial import Serial, SerialException  # Updated import

def main():
    if len(sys.argv) != 3:
        print("Usage: {} <serial_port> <baudrate>".format(sys.argv[0]))
        sys.exit(1)
    serial_port_name = sys.argv[1]
    baudrate = int(sys.argv[2])

    try:
        # Open the serial port
        serial_port = Serial(serial_port_name, baudrate)  # Updated usage
    except SerialException as e:  # Updated usage
        print(f"Failed to open serial port {serial_port_name}: {e}")
        sys.exit(1)

    # Create a TCP server socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(('localhost', 1234))
    server_socket.listen(1)
    print("Waiting for GDB connection on port 1234...")

    try:
        # Accept a connection from GDB
        client_socket, addr = server_socket.accept()
        print("GDB connected from", addr)

        while True:
            # Use select to wait for data on either socket
            rlist, _, _ = select.select([serial_port, client_socket], [], [])

            for s in rlist:
                if s == serial_port:
                    # Read from serial port and send to GDB
                    data = serial_port.read(serial_port.in_waiting or 1)
                    if data:
                        client_socket.sendall(data)
                elif s == client_socket:
                    # Read from GDB and send to serial port
                    data = client_socket.recv(1024)
                    if data:
                        serial_port.write(data)
                    else:
                        # GDB disconnected
                        print("GDB disconnected")
                        client_socket.close()
                        return
    except KeyboardInterrupt:
        print("Interrupted by user")
    finally:
        client_socket.close()
        serial_port.close()
        server_socket.close()

if __name__ == '__main__':
    main()
