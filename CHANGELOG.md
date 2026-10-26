# Changelog

All notable changes to CKLOS are documented here.

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
