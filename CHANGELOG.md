# Changelog

All notable changes to CKLOS are documented here.

## 0.16.0 — 2026-10-29

### Added

- Multi-stream IPv4 TCP with six concurrent streams, three guest listeners,
  per-stream handshakes, FIN/RST handling and asynchronous outbound connects.
- Independent SSH channel state, per-channel local/remote flow-control windows
  and multiple concurrent session, forwarding and SFTP channels.
- Real OpenSSH `ssh -L` local forwarding, including simultaneous `-L` tunnels.
- Real OpenSSH `ssh -R` remote forwarding with `tcpip-forward`,
  `cancel-tcpip-forward` and `forwarded-tcpip`.
- SSH agent forwarding through `auth-agent@openssh.com`, a guest
  `AF_UNIX`/`SOCK_STREAM` endpoint at `/tmp/cklos-ssh-agent.sock`, and
  `SSH_AUTH_SOCK` in the ring-3 environment.
- `/bin/USERAGENT.ELF`, a ring-3 agent protocol client using `socket`,
  `connect`, `read`, `write` and `poll`.
- A 32 KiB, cache-aligned RTL8139 receive ring for reliable bulk SSH transfers.
- Dynamic 128 KiB kernel exception stacks, removing the fixed exception-stack
  overflow encountered by network-heavy syscalls.

### Fixed

- Moved the SSH channel table from low BSS into the heap so kernel state no
  longer overlaps the legacy BIOS/VGA ROM window around `0xA0000`.
- Corrected DIRECT-TCPIP port parsing from `uint16` to the RFC 4254 `uint32`.
- Corrected SFTP v3 READ/WRITE offsets from `uint32` to the specified `uint64`.
- Corrected channel request replies to target the peer's client channel ID.
- Fixed the Ed25519 signature blob stack allocation that could corrupt the
  SSH host-key exchange after memory-layout changes.
- Removed recursive SSH packet parsing during blocking forwarding setup.
- Restored command-channel EOF handling so one-shot exec commands can finish
  after the client closes its stdin.

## 0.15.1 — 2026-10-28

### Fixed

- Default VBE mode is now 1280x800, with deterministic 1280x720, 1024x768,
  800x600 and VGA fallbacks.
- Mouse movement and button input continue through PS/2 controller polling
  after leaving `cki`, instead of freezing until a keyboard event.
- `SYS` and `NET` panels toggle on a second click. `CLR` closes any open panel
  and clears the terminal window.

## 0.15.0 — 2026-10-27

### Added

- SFTP v3 over SSH with INIT/VERSION, OPEN, READ, WRITE, STAT/LSTAT/FSTAT,
  OPENDIR/READDIR, REMOVE, MKDIR, RMDIR, RENAME and handle management.
- Bidirectional SSH channel window accounting, WINDOW_ADJUST generation and
  packet sends limited by the peer's remaining window.
- `auth-agent-req@openssh.com` acceptance and DIRECT-TCPIP channel negotiation.

### Known limitations

- TCP forwarding negotiation exists but connecting a forwarded channel to an
  outbound target still fails validation.
- Agent forwarding control negotiation exists, but no guest Unix-domain agent
  socket proxy is available.
- Remote forwarding (`tcpip-forward`) is not implemented.

## 0.14.0 — 2026-10-27

### Added

- Encrypted SSH-2 server on guest port 22 with QEMU forwarding to host port
  2222.
- Curve25519 key exchange, Ed25519 host keys and authorized-key authentication.
- AES-128-CTR and HMAC-SHA256 transport, PTY/shell/exec session channels.
- OpenSSH integration test covering authentication and remote `uname`.

## 0.13.1 — 2026-10-27

### Fixed

- `cki` now clears and paints its complete editor surface, preventing stale
  terminal colors and yellow shell text from bleeding through document text.
- The editor uses a high-contrast title bar, toolbar, line-number gutter and
  status bar with clickable Save, Open, Find, Undo, Quit and close controls.
- PS/2 Ctrl combinations now produce real control keys, enabling Ctrl+S,
  Ctrl+O, Ctrl+F, Ctrl+Z and Ctrl+Q in the graphical editor.
- The software mouse cursor is invalidated and redrawn after editor frames.
- Removed the non-functional terminal `_ [] X` decoration from the shell chrome.

## 0.13.0 — 2026-10-27

### Added

- `make cxx-eval`: an executable Clang/LLVM freestanding C++23 frontend probe.
- Separate Clang/LLVM and GCC C++ runtime-porting reports, dependency lists,
  risk analysis and engineering estimates.

### Scope

- This batch evaluates C++ frontends only. CKLOS still has no C++ compiler or
  C++ standard library, and `cck` remains strictly a C compiler.

## 0.12.0 — 2026-10-27

### Added

- Outgoing IPv4 TCP clients, IPv4 UDP and DNS A/AAAA resolution.
- `dns`, `cwgt` and `curl` shell tools with file/stdout output.
- HTTP/1.1 downloads.
- BearSSL static TLS 1.2 client, SNI and X.509 test trust anchor.
- HTTPS downloads through `curl`.
- HTTP redirects, POST request bodies, Basic authentication, cookies, custom
  headers, user agents, referrers, ranges, resumable output and transfer timeouts.
- Deterministic HTTP/HTTPS fixture server covering redirects, POST, auth,
  ranges and request headers.
- Pinned Mozilla/curl CA bundle and a bounded multi-anchor BearSSL trust store.
- IPv6 neighbor discovery, outgoing TCPv6, dual-stack URL transport and an
  Ethernet-level IPv6 HTTP integration peer.
- Automated `cki` interaction test that types, saves and reopens UTF-8 text.

### Known limitations

- IPv6 DNS is present, but IPv6 packet transmission is not complete.
- Every GNU wget/curl option is not yet supported.
- `cki` uses post-editor PS/2 polling and leaves PIT interrupts masked until
  reboot; this avoids a kernel interrupt-return fault that still requires a
  dedicated scheduler repair.

## 0.11.0 — 2026-10-27

### Added

- VBE 1024x768x32 linear graphics with 800x600 and VGA fallbacks.
- Dynamic high-resolution GUI layout and bounded high-resolution mouse cursor.
- Pinned GNU Unifont 18.0.01/WQY CJK bitmap data and UTF-8 rendering.
- Non-modal full-screen `cki` editor with Chinese paths and content.
- Editor save/open/find/undo, mouse caret positioning, scrolling and UTF-8 deletion.

## 0.10.0 — 2026-10-27

### Added

- Native ring-3 `cck` C compiler using pinned TinyCC 0.9.28rc.
- C89/C90, C99, C11, C17, C23 and GNU mode selection; explicit rejection of `c98`.
- In-system C compilation to static ELF64 and default `a.rks`.
- RKS2 container with BearSSL SHA-256 and kernel-side integrity verification.
- CKLOS SDK `<stdbit.h>` and `<stdckdint.h>` headers.
- musl headers, archives, TinyCC runtime files and license provenance in FS2.

## 0.9.0 — 2026-10-27

### Added

- ELF64 static user executable loader and ring-3 execution.
- Per-process page tables, CR3 switching and supervisor/user isolation.
- Kernel/user GDT, 64-bit TSS and `int 0x80` system-call gate.
- `fork`, `execve`, `wait4`, `exit`, `getpid`, `kill`, `yield` and timer preemption.
- `mmap`, `brk`, file descriptors, FS2-backed file I/O, pipes and `poll`.
- SSE/x87 context switching for user processes.
- Pinned musl static libc, public headers and archives installed into CKLOSFS2.
- User-side ELF fault, fork, exec, file and pipe regression programs.

## 0.8.0 — 2026-10-27

### Added

- Two-stage BIOS bootloader that safely loads kernels across 64 KiB boundaries.
- 128 MiB CKLOSFS2 volume backed by pinned LittleFS 2.11.3.
- Hierarchical directories, UTF-8 paths and variable-length files up to 64 MiB.
- `FSLS`, `FSCD`, `FSPWD`, `FSMKDIR`, `FSWRITE`, `FSCAT`, `FSMV` and `FSRM`.
- `FSFILL`, non-overwriting CKLOSFS1 migration through `FSMIGRATE`, and `FSCHECK`.
- Automated large-file, deep-directory, UTF-8 and cross-reboot FS2 tests.

### Fixed

- The second-stage loader now advances its BIOS segment by 64 KiB, not 512 bytes.
- Freestanding LittleFS builds no longer emit unavailable SSE instructions.
- FS2 superblock validation now checks version, geometry and checksum.

## 0.7.0 — 2026-10-26

### Added

- RKS1 x86-64 executable format with version, size and checksum validation.
- Runtime loader and ABI for text, timers, sleep and mouse access.
- External `HELLO.RKS` executable stored in CKLOSFS.
- `EXEC` and `RKS` shell commands.

## 0.6.0 — 2026-10-25

### Added

- x86-64 long-mode boot path with identity page tables and 64-bit kernel ABI.
- 64-bit IDT, interrupt frames, task stacks and context switching.
- PS/2 IRQ12 mouse driver with a visible software cursor.
- Clickable SYS, NET and CLR controls plus graphical information panels.
- QEMU `show-cursor` launch option so clicking the window does not hide the pointer.

## 0.5.0 — 2026-10-25

### Added

- VGA mode 13h 320×200 graphical framebuffer and windowed terminal.
- Public-domain 8×8 bitmap font rendering, scrolling and graphical color handling.
- RTL8139 PCI networking, ARP, IPv4, ICMP and TCP diagnostics.
- QEMU host forwarding for ports 2222 and 2323.
- SSH-2/cryptography development source and pinned Monocypher/BearSSL dependencies.

### Known limitations

- SSH port 22 remains disabled until the large-kernel boot path passes OpenSSH interoperability tests.

## 0.4.0 — 2026-10-25

### Added

- E820 physical-memory discovery, identity paging, page allocator and kernel heap.
- Interrupt-driven PS/2 keyboard and preemptive kernel task scheduler.
- ATA PIO storage with CKLOSFS1 persistence.
- Script execution through `RUN`, plus `CP`, `WC`, `HEAD`, `TAIL`, `KILL` and `KILLALL`.
- MIT license, architecture documentation, roadmap and contribution guide.
- Portable Makefile tool discovery for macOS and Linux.

### Changed

- Version information is centralized in `src/version.h`.
- Kernel diagnostic output now includes memory, heap and task statistics.

## 0.3.0 — 2026-10-25

### Added

- BIOS memory map, paging, page-frame allocator and kernel heap.
- Preemptive task scheduling and interrupt-based keyboard input.
- Persistent ATA data disk.

## 0.2.0 — 2026-10-25

### Added

- IDT/PIC/PIT foundation and CPU exception handling.
- Writable RAM file system, command history and shell utilities.

## 0.1.0 — 2026-10-25

### Added

- Initial bootable 32-bit CKLOS kernel, VGA/serial console and command shell.
