#!/usr/bin/env python3
"""Small deterministic HTTP fixture used by the CKLOS integration test."""

import argparse
import base64
import http.server
import pathlib
import socket
import ssl
import sys
import urllib.parse


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def send_bytes(self, status, body, content_type="text/plain", **headers):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers.items():
            self.send_header(name.replace("_", "-"), value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        self.send_bytes(200, b"posted:" + body)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path == "/redirect":
            self.send_bytes(302, b"", Location="/cck_hello.c")
            return
        if parsed.path == "/auth":
            expected = "Basic " + base64.b64encode(b"user:pass").decode()
            if self.headers.get("Authorization") != expected:
                self.send_bytes(401, b"authentication required\n")
                return
            self.send_bytes(200, b"authenticated\n")
            return
        if parsed.path == "/cookie":
            self.send_bytes(200, b"cookie set\n", Set_Cookie="cklos=yes; Path=/")
            return
        if parsed.path == "/echo":
            body = (
                f"method={self.command}\n"
                f"user-agent={self.headers.get('User-Agent', '')}\n"
                f"header={self.headers.get('X-CKLOS-Test', '')}\n"
            ).encode()
            self.send_bytes(200, body)
            return
        if parsed.path == "/range":
            data = (b"0123456789abcdef" * 4)
            header = self.headers.get("Range", "")
            if header.startswith("bytes="):
                start = int(header[6:].split("-", 1)[0])
                data = data[start:]
                self.send_bytes(206, data, Accept_Ranges="bytes")
            else:
                self.send_bytes(200, data, Accept_Ranges="bytes")
            return
        if parsed.path == "/missing":
            self.send_bytes(404, b"missing\n")
            return
        super().do_GET()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    server_class = http.server.ThreadingHTTPServer
    if ":" in args.bind:
        class IPv6Server(server_class):
            address_family = socket.AF_INET6
        server_class = IPv6Server
    server = server_class(
        (args.bind, args.port),
        lambda *a, **k: Handler(*a, directory=args.root, **k),
    )
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
