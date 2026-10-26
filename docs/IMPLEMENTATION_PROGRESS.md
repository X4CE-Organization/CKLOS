# CKLOS Toolchain Implementation Progress

This file is the source of truth for the batched native-toolchain implementation.

| Batch | Version | Status |
| --- | --- | --- |
| CKLOSFS2 and directories | 0.8.0 | complete and tested |
| ELF64, ring 3, syscalls, libc | 0.9.0 | complete and tested |
| TinyCC and cck | 0.10.0 | complete and tested |
| VBE, Unicode and cki | 0.11.0 | next |
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
