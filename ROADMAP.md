# CKLOS Roadmap

## 0.4 — Open-source kernel foundation

- [x] E820, paging, page allocator and kernel heap
- [x] IDT, PIC, PIT and interrupt-driven keyboard
- [x] Preemptive round-robin kernel tasks
- [x] ATA persistence and CKLOSFS1
- [x] Command scripts and expanded shell utilities
- [x] MIT license, architecture docs, contribution guide and CI

## 0.5 — User mode and programs

- [ ] User code/data GDT entries and TSS
- [ ] Per-process address spaces with 4 KiB U/S page tables
- [ ] System-call gate and ABI documentation
- [ ] ELF32 loader and process lifecycle
- [ ] Fault isolation between processes

## 0.6 — Device and file-system layer

- [ ] VFS abstraction and directory tree
- [ ] ATA DMA and partition discovery
- [ ] RTC abstraction and wall-clock timers
- [ ] PS/2 mouse driver
- [ ] VBE/VESA framebuffer discovery

## 0.7 — Graphics foundation

- [x] Framebuffer console and windowed terminal
- [x] Mouse cursor and clickable GUI controls
- [ ] Double-buffered 2D drawing API
- [ ] Font and image loading
- [ ] Compositor and window model

A basic graphical terminal is now active. Future work adds application windows, mouse events and a compositor.

## 1.0 — Usable desktop

- [ ] Window manager and application launcher
- [ ] Persistent hierarchical file system
- [ ] Network driver, IP, UDP and TCP
- [ ] Security review and reproducible releases
