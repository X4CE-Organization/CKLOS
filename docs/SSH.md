# CKLOS SSH

CKLOS includes an SSH-2 server on guest TCP port 22. The default QEMU rule
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

## Protocol support

- SSH-2 version exchange and packet framing
- `curve25519-sha256` key exchange
- `ssh-ed25519` host and user keys
- `aes128-ctr` encryption and `hmac-sha2-256` authentication
- Multiple independent SSH channels with per-channel local and remote windows
- PTY requests, shell sessions, one-shot commands and `WINDOW_ADJUST`
- SFTP v3 upload, download, directory listing, metadata, rename and removal
- Local forwarding through `direct-tcpip`
- Remote forwarding through `tcpip-forward`, `cancel-tcpip-forward` and
  `forwarded-tcpip`
- Agent forwarding through `auth-agent-req@openssh.com` and
  `auth-agent@openssh.com`

Cryptography is provided by the pinned Monocypher and BearSSL implementations.
The server accepts one SSH transport connection at a time; each transport can
carry multiple concurrent channels.

## Local forwarding

Forward a host-side port through CKLOS to a target reachable from the guest:

```sh
ssh -N -i keys/cklos_user -p 2222 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -L 127.0.0.1:18090:10.0.2.2:18081 \
    cklos@127.0.0.1

curl http://127.0.0.1:18090/file
```

Multiple `-L` options can be used together. Each local connection receives an
independent channel and flow-control window.

## Remote forwarding

Start QEMU with an additional host-forwarded port for the guest listener:

```sh
qemu-system-x86_64 -m 64M -cpu max \
    -drive file=build/cklos.img,format=raw,if=floppy -boot a \
    -drive file=build/cklos-data.img,format=raw,if=ide,index=0 \
    -drive file=build/cklos-fs2.img,format=raw,if=ide,index=1 \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22,hostfwd=tcp:127.0.0.1:18096-:18082 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:56
```

Then request a listener on guest port 18082:

```sh
ssh -N -i keys/cklos_user -p 2222 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -R 0.0.0.0:18082:127.0.0.1:18081 \
    cklos@127.0.0.1

curl http://127.0.0.1:18096/file
```

Port zero is supported by the SSH global-request protocol; the assigned port
is returned in the success reply and by `cancel-tcpip-forward`.

## Agent forwarding

Enable agent forwarding with `ssh -A`:

```sh
eval "$(ssh-agent -s)"
ssh-add keys/cklos_user
ssh -A -i keys/cklos_user -p 2222 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    cklos@127.0.0.1 agent
```

The `agent` shell command opens `auth-agent@openssh.com` and reports the number
of identities supplied by the host agent. Ring-3 programs receive:

```text
SSH_AUTH_SOCK=/tmp/cklos-ssh-agent.sock
```

The CKLOS syscall ABI implements a single `AF_UNIX`/`SOCK_STREAM` endpoint for
that path with `socket`, `connect`, `read`, `write`, `poll` and `close`
semantics. `/bin/USERAGENT.ELF` demonstrates a full agent-protocol exchange:

```sh
ssh -A ... cklos@127.0.0.1 'elfexec /bin/USERAGENT.ELF'
```

An expected result is:

```text
SSH_AUTH_SOCK=/tmp/cklos-ssh-agent.sock
AGENT_USER: identities=1
```

## SFTP

The standard SFTP subsystem is available:

```sh
sftp -i keys/cklos_user -P 2222 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    cklos@127.0.0.1
```

Transfer sizes are bounded by CKLOSFS2 file and disk limits. Uploads write
directly to the persistent 128 MiB filesystem.

## Deliberate limits

- One SSH transport connection is accepted at a time.
- Password, keyboard-interactive and certificate authentication are not
  implemented.
- Dynamic forwarding (`-D`) and connection multiplexing are not implemented.
- The guest endpoint is an `AF_UNIX` stream adapter backed by one forwarded
  connection; it does not expose arbitrary guest socket files.
