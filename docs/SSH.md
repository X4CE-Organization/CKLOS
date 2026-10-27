# CKLOS SSH

CKLOS includes an SSH-2 server on guest TCP port 22. The QEMU launch rule
forwards host `127.0.0.1:2222` to that port.

The first build creates a development key pair under `keys/`. The authorized
user is `cklos`, and authentication is public-key-only:

```sh
make keys
ssh -i keys/cklos_user -p 2222 \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    cklos@127.0.0.1
```

Supported protocol pieces:

- SSH-2 version exchange and binary packet framing
- `curve25519-sha256` key exchange
- `ssh-ed25519` host key and public-key authentication
- `aes128-ctr` transport encryption
- `hmac-sha2-256` packet authentication
- session channels, PTY requests, shell requests and one-shot exec requests
- channel data/EOF/close/status handling
- SFTP v3 upload, download, listing, metadata, rename and directory operations
- peer-window accounting and local WINDOW_ADJUST flow control

Agent-forwarding requests are acknowledged, and DIRECT-TCPIP channel
negotiation is present. Guest-side Agent sockets and working forwarded target
connections are not complete yet; remote `tcpip-forward` is also pending.

Cryptography is provided by pinned Monocypher and BearSSL implementations.
The server currently accepts one session at a time and does not implement
password authentication, port forwarding, agent forwarding, SFTP, multiple
concurrent SSH clients or window-flow-control backpressure.
