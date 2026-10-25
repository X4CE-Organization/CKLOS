# Changelog

All notable changes to CKLOS are documented here.

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
