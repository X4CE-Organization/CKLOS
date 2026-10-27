#!/usr/bin/env python3
"""Minimal IPv6/NDP/TCP peer for the deterministic CKLOS QEMU test."""

import socket
import struct

LOCAL_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("525400123457")
LOCAL_IP = bytes.fromhex("fec00000000000000000000000000015")
PEER_IP = bytes.fromhex("fec00000000000000000000000000002")
BODY = (
    b"#include <stdio.h>\n\n"
    b"int main(void) {\n"
    b"    return 0;\n"
    b"}\n"
)


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def ipv6_packet(destination, next_header, payload):
    header = struct.pack("!IHBB", 0x60000000, len(payload), next_header, 64)
    return header + PEER_IP + destination + payload


def ethernet(destination, payload):
    return destination + PEER_MAC + struct.pack("!H", 0x86DD) + payload


def tcp_checksum(source, destination, segment):
    pseudo = source + destination + struct.pack("!I3xB", len(segment), 6)
    return checksum(pseudo + segment)


def tcp_segment(destination, source_port, destination_port, sequence,
                acknowledgement, flags, payload=b""):
    segment = struct.pack(
        "!HHIIBBHHH",
        source_port,
        destination_port,
        sequence,
        acknowledgement,
        5 << 4,
        flags,
        4096,
        0,
        0,
    ) + payload
    value = tcp_checksum(PEER_IP, destination, segment)
    return segment[:16] + struct.pack("!H", value) + segment[18:]


def read_frame(connection):
    header = connection.recv(4)
    if len(header) != 4:
        return None
    length = struct.unpack("!I", header)[0]
    data = b""
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def write_frame(connection, frame):
    connection.sendall(struct.pack("!I", len(frame)) + frame)


def main():
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 19090))
    listener.listen(1)
    connection, _ = listener.accept()
    connection.settimeout(20)
    guest_port = 0
    expected_sequence = 0
    sent_sequence = 5000
    response_sent = False
    try:
        while True:
            frame = read_frame(connection)
            if frame is None or len(frame) < 54 or frame[12:14] != b"\x86\xdd":
                continue
            ip = frame[14:]
            if ip[6] == 58 and ip[40] == 135:
                target = ip[48:64]
                if target != PEER_IP:
                    continue
                destination = ip[8:24]
                advertisement = bytearray(32)
                advertisement[0] = 136
                advertisement[4] = 0x60
                advertisement[8:24] = PEER_IP
                advertisement[24:26] = b"\x01\x01"
                advertisement[26:32] = PEER_MAC
                advertisement[2:4] = struct.pack(
                    "!H",
                    checksum(destination + ip[24:40] + b"\0\0\0\x3a"
                             + bytes(advertisement)),
                )
                write_frame(
                    connection,
                    ethernet(
                        LOCAL_MAC,
                        ipv6_packet(destination, 58, bytes(advertisement)),
                    ),
                )
                continue
            if ip[6] != 6:
                continue
            tcp = ip[40:]
            source_port, destination_port, sequence, acknowledgement = (
                struct.unpack("!HHII", tcp[:12])
            )
            flags = tcp[13]
            header_length = (tcp[12] >> 4) * 4
            payload = tcp[header_length:]
            destination = ip[8:24]
            if flags & 0x02:
                guest_port = source_port
                expected_sequence = sequence + 1
                reply = tcp_segment(
                    destination,
                    destination_port,
                    guest_port,
                    sent_sequence,
                    expected_sequence,
                    0x12,
                )
                write_frame(
                    connection, ethernet(LOCAL_MAC,
                                         ipv6_packet(destination, 6, reply))
                )
                sent_sequence += 1
            elif payload and not response_sent:
                expected_sequence += len(payload)
                response = (
                    b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                    + f"Content-Length: {len(BODY)}\r\nConnection: close\r\n\r\n"
                    .encode()
                    + BODY
                )
                reply = tcp_segment(
                    destination,
                    destination_port,
                    guest_port,
                    sent_sequence,
                    expected_sequence,
                    0x19,
                    response,
                )
                write_frame(
                    connection, ethernet(LOCAL_MAC,
                                         ipv6_packet(destination, 6, reply))
                )
                sent_sequence += len(response)
                response_sent = True
    finally:
        connection.close()
        listener.close()


if __name__ == "__main__":
    main()
