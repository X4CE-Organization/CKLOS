# CKLOS x86-64 Syscall ABI

User programs enter the kernel with `int 0x80`. The register convention follows
the Linux x86-64 subset:

| Register | Meaning |
| --- | --- |
| `rax` | Syscall number and return value |
| `rdi` | Argument 1 |
| `rsi` | Argument 2 |
| `rdx` | Argument 3 |
| `r10` | Argument 4 |
| `r8` | Argument 5 |
| `r9` | Argument 6 |

Errors are returned as negative errno values. User pointers are page-walked and
must map through user-accessible 4 KiB pages. Kernel addresses, page tables and
MMIO remain supervisor-only.

Implemented calls include:

- `read`, `write`, `writev`, `open`, `close`, `pipe`, `dup`, `dup2`
- `mmap`, `munmap`, `brk`
- `getpid`, `fork`, `execve`, `wait4`, `exit`, `exit_group`, `kill`
- `sched_yield`, `poll`, `ioctl`
- `arch_prctl`, `set_tid_address`, `set_robust_list`, `rseq`

`execve` currently accepts static ELF64 CKLOS images. Dynamic linking, Linux
binary compatibility, full signal delivery and full POSIX terminal semantics
remain outside this ABI revision.
