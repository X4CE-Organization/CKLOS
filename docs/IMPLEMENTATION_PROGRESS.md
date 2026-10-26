# CKLOS Toolchain Implementation Progress

This file is the source of truth for the batched native-toolchain implementation.

| Batch | Version | Status |
| --- | --- | --- |
| CKLOSFS2 and directories | 0.8.0 | complete and tested |
| ELF64, ring 3, syscalls, libc | 0.9.0 | next |
| TinyCC and cck | 0.10.0 | pending |
| VBE, Unicode and cki | 0.11.0 | pending |
| IPv4/IPv6, cwgt and curl | 0.12.0 | pending |
| Clang/GCC C++ evaluation | 0.13.0 | pending |

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
