# CKLOS Networking and Transfer Tools

## Implemented transport

- RTL8139 PCI Ethernet
- ARP and IPv4
- ICMP echo
- IPv4 TCP listener and outgoing client
- IPv4 UDP
- DNS A and AAAA queries through QEMU's DNS server
- `/etc/hosts` lookup and direct numeric IPv4 addresses
- HTTP/1.1 GET, POST, redirects, ranges and `Connection: close`
- BearSSL TLS 1.2 client with SNI, X.509 verification and RSA/ECDSA profiles

The test image installs a generated trust anchor at
`/etc/ssl/certs/cklos-test-ca.der`. It is used only by the HTTPS integration
test. A multi-anchor Mozilla CA bundle loader remains future work; the current
TLS client does not yet claim universal public-web certificate compatibility.

## `cwgt`

```text
CKLOS> cwgt -O /home/cklos/file.txt http://10.0.2.2:8080/file.txt
CKLOS> cwgt -q https://example.test/file
```

Supported core behavior: URL fetch, `-O FILE`/`-P`, quiet/verbose output,
redirect following, resumable transfer, custom headers, user agent, POST data
and saving response bodies to CKLOSFS2.

## `curl`

```text
CKLOS> curl https://example.test/file
CKLOS> curl -v -o /home/cklos/file https://example.test/file
CKLOS> curl -O http://10.0.2.2:8080/file.txt
```

Supported core behavior: GET/HEAD/custom methods, POST bodies, stdout/file
output, `-o`, `-O`, `-L`, `-i`, `-s`, `-S`, `-v`, `-f`, `-u`, `-A`, `-e`,
`-b`, `-H`, `--range`, `-C -`, timeouts and `--url`. The current implementation
does not yet implement every GNU curl transfer option, HTTP/2, HTTP/3, FTP,
WebSocket, multipart upload, cookie jars, compression, arbitrary authentication
or proxy modes.

## `dns`

`dns HOST` prints A and AAAA records when available.

IPv6 AAAA lookup is implemented, but IPv6 packet transmission and IPv6 HTTP
connections are not yet complete. HTTP transfer currently uses IPv4. HTTPS
currently trusts the CKLOS test CA installed in the test image; a public CA
bundle loader remains required for general Internet use.
