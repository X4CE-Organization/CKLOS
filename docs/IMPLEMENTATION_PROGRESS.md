# CKLOS Toolchain Implementation Progress

This file is the source of truth for the batched native-toolchain implementation.

| Batch | Version | Status |
| --- | --- | --- |
| CKLOSFS2 and directories | 0.8.0 | complete and tested |
| ELF64, ring 3, syscalls, libc | 0.9.0 | complete and tested |
| TinyCC and cck | 0.10.0 | complete and tested |
| VBE, Unicode and cki | 0.11.0 | complete and tested |
| IPv4/IPv6, cwgt and curl | 0.12.0 | core complete and tested; extended curl options pending |
| Clang/GCC C++ evaluation | 0.13.0 | complete: probe and reports, no C++ compiler |
| SSH-2 server | 0.14.0 | complete and tested with OpenSSH |

Rules:

- Advance only the first non-complete batch.
- Run `make check` and the batch-specific tests.
- Commit and push `main` only when all tests pass.
- Keep failed work in the working tree and report the failure.
- Do not delete user data or force-push.

## 0.8.0 checkpoint

Completed:

- Pinned LittleFS 2.11.3 at `6cb4e86540eca0d9ba62500a298385c9d863c8be`
  with its BSD-3-Clause license retained in the fetched source tree.
- Host-side 128 MiB CKLOSFS2 image builder with directory tree and variable files.
- Factory tree: `/bin`, `/etc`, `/home/cklos`, `/import`, `/usr/include`, `/usr/lib`.
- Kernel-side LittleFS mount, superblock validation and path API for
  list/read/write/mkdir/remove/rename.
- Commands: `FSLS`, `FSCD`, `FSPWD`, `FSMKDIR`, `FSWRITE`, `FSCAT`,
  `FSRM`, `FSMV`, `FSFILL`, `FSMIGRATE` and `FSCHECK`.
- Two-stage BIOS bootloader with 200-sector kernel capacity, a 32-bit
  protected-mode transition and E820 forwarding to the 64-bit kernel.
- Automated deep-directory, 32 KiB variable file, UTF-8 path, atomic rename,
  FS1 migration and cross-reboot persistence tests.

Verification:

- `make test` passes both QEMU boots.
- `FSCHECK` reports valid metadata after creation and after reboot.
- No 0.8.0 data was pushed before these tests passed.

## 0.9.0 checkpoint

Completed:

- ELF64 static executable loader at user address `0x4000000000`.
- Ring-3 user code, user stack near `0x7FFFFFF000`, private PML4 and CR3 per process.
- Kernel/user GDT entries, 64-bit TSS and a dedicated supervisor exception stack.
- `int 0x80` syscall gate with user-pointer validation and per-process page checks.
- `fork`, `execve`, `wait4`, `exit`, `getpid`, `kill` and timer preemption.
- Anonymous `mmap`, `brk`, file descriptors, file I/O, pipes and `poll`.
- SSE/x87 state saving and restoring across user process switches.
- Pinned musl source built as a freestanding static libc with CKLOS syscall glue.
- musl headers, archives and license installed into CKLOSFS2.
- User exceptions terminate only the faulting process; the shell resumes.

Verification:

- Plain ring-3 ELF execution and cooperative syscall round trip.
- Fork, wait, child status delivery and cross-process pipe communication.
- Child `execve` image replacement and inherited file descriptor table.
- User file open/write/read/close through CKLOSFS2.
- Supervisor-memory access terminates the process with `make test` still passing.
- Static musl `printf`, `malloc`, `strcpy`, `getpid` and buffered output.

## 0.10.0 checkpoint

Completed:

- Pinned TinyCC `e23dabed154a8495f3ac1ff3cc9d04fa1c207893`.
- Native ring-3 `/bin/cck` linked against CKLOS's static musl.
- Shell-compatible `CCK` command with argument passing and compiler options.
- Accepted modes from C89/C90 through C17/C23 and matching GNU modes.
- Explicit `-std=c98` rejection with a C++98 diagnostic.
- Static ELF64 output and default `a.rks` output.
- RKS2 header, version, payload size, flags and BearSSL SHA-256 verification.
- Kernel execution of RKS2 by extracting and validating the enclosed ELF64.
- CKLOS SDK headers for `<stdbit.h>` and `<stdckdint.h>`.
- TinyCC and musl runtime, headers, archives and license files in CKLOSFS2.

Verification:

- All declared standard/GNU modes compile the bundled C test.
- `cck` creates `a.rks` with no `-o`.
- The generated RKS2 runs in ring 3 and prints through musl stdio.

Known limitation:

- TinyCC does not implement the entire C23 language surface. The modes and
  headers are installed, but full external conformance-suite coverage remains
  ongoing rather than being represented as ISO certification.

## 0.11.0 checkpoint

Completed:

- VBE 1024x768x32 linear framebuffer, with 800x600 and VGA mode 13h fallbacks.
- Stage-two framebuffer discovery, mode selection and boot-info handoff.
- Dynamic GUI layout, high-resolution mouse bounds and 32/16/8-bit rendering.
- Pinned GNU Unifont 18.0.01 plus WQY CJK glyph data generated into FS2.
- UTF-8 decoding, encoding, previous-code-point navigation and CJK text drawing.
- Non-modal `cki` editor with line numbers, mouse caret placement, scrolling,
  find, save, open, UTF-8 backspace and last-edit undo.
- 8 MiB editor buffer and CRLF-to-LF normalization.

Verification:

- QEMU reports `VBE linear 1024x768x32`.
- `cki` creates and reopens Chinese text through CKLOSFS2.
- UTF-8 content survives the second QEMU boot.

Known limitation:

- `cki` currently runs as a protected kernel GUI service rather than a ring-3
  client of a future compositor/windowing protocol.

## 0.12.0 checkpoint

Completed:

- Outgoing IPv4 TCP clients alongside the existing TCP listener.
- IPv4 UDP and DNS A/AAAA parsing.
- `DNS HOST` command.
- Public-IP parser, `/etc/hosts` lookup and QEMU DNS resolution.
- `CWGT` and `CURL` commands with stdout/file output.
- HTTP/1.1 GET/POST, redirects, ranges, Basic Auth, cookies and custom headers.
- Pinned, freestanding BearSSL client library and HTTPS GET.
- SNI and X.509 verification against an installed test trust anchor.
- Automated UTF-8 `cki` interaction, not only direct filesystem writes.

Verification:

- Local HTTP server download into CKLOSFS2.
- Local HTTPS server download through BearSSL.
- Public DNS A and AAAA response parsing.
- IPv6 NDP and TCPv6 HTTP transfer against a deterministic Ethernet peer.
- Downloaded files are listed after reboot.

Remaining network work:

- Full GNU `cwgt`/`curl` option compatibility, persistent cookie jars,
  multipart upload, proxies and compressed responses.
- A dedicated kernel interrupt-return repair so editor polling can return to
  timer-driven scheduling after close.

## 0.13.0 checkpoint

Completed:

- Executable Clang/LLVM C++23 freestanding frontend probe.
- Separate Clang and GCC impact, dependency, licensing and schedule reports.
- Explicit statement that `cck` is C-only and that C++98 was not silently
  accepted as C.

Not delivered:

- No C++ compiler, libc++, libstdc++, exception runtime or C++ ABI is claimed
  as part of this checkpoint.

## 0.14.0 checkpoint

Completed:

- SSH-2 server on guest port 22 with QEMU host port 2222.
- Curve25519 key exchange, Ed25519 host key/signatures and authorized-key auth.
- AES-128-CTR plus HMAC-SHA256 packet transport.
- PTY, shell and one-shot exec channels integrated with the CKLOS shell.

Verification:

- OpenSSH 9.9 completed key exchange, host signature verification, public-key
  authentication, remote `uname`, exit status and channel close.

## 0.16.0 checkpoint

Completed:

- Multi-stream TCP and independent SSH channel/window state.
- Real OpenSSH local forwarding, remote forwarding and concurrent local tunnels.
- Agent forwarding with a ring-3 `AF_UNIX` stream endpoint and inherited
  `SSH_AUTH_SOCK`.
- SFTP v3 regression coverage on the multi-channel transport.

Verification:

- OpenSSH 9.9 transferred HTTP content through simultaneous `ssh -L` tunnels
  and through `ssh -R`.
- The shell and `/bin/USERAGENT.ELF` each queried identities from a host
  `ssh-agent` through forwarded `auth-agent@openssh.com` channels.
