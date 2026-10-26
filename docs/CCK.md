# cck C Compiler

`cck` is CKLOS's native C compiler command. It runs as a ring-3 process and
uses a pinned TinyCC backend:

- TinyCC commit: `e23dabed154a8495f3ac1ff3cc9d04fa1c207893`
- Backend version: 0.9.28rc
- License: LGPL-2.1, preserved in `/usr/share/licenses/tinycc/COPYING`
- Rebuild source: `scripts/fetch-tinycc.sh` and `scripts/build-cck.sh`

The compiler accepts `-std=c89`, `gnu89`, `c90`, `gnu90`, `c99`, `gnu99`,
`c11`, `gnu11`, `c17`, `gnu17`, `c23` and `gnu23`. `-std=c98` is rejected
because C98 is not a C standard; C++98 is a separate C++ frontend issue.

Examples:

```text
CKLOS> cck -std=c17 -O2 -o /home/cklos/app.elf /home/cklos/app.c
CKLOS> cck -std=c23 -o /home/cklos/app.rks /home/cklos/app.c
CKLOS> cck /home/cklos/app.c
CKLOS> elfexec /home/cklos/app.rks
```

An output ending in `.rks` is wrapped in an RKS2 container containing:

- 64-byte versioned header
- ELF64 payload size and flags
- SHA-256 over the ELF payload, generated with BearSSL
- Static ELF64 payload

The kernel validates RKS2 size, ELF magic and SHA-256 before loading it. With no
`-o`, `cck` emits `a.rks`.

TinyCC's parser is not a complete implementation of every C23 language
feature. `cck` supplies the standard-mode matrix, musl headers and CKLOS SDK
headers including `<stdbit.h>` and `<stdckdint.h>`, but C23 conformance gaps
remain tracked work; the command does not claim to be GCC or Clang.
