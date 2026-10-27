# C++ Frontend Evaluation

This document is the 0.13.0 feasibility deliverable. It does not claim that
CKLOS has a working C++ compiler, standard library, or Linux C++ ABI.

`cck` remains a C compiler. `-std=c98` remains invalid and is reported as a
possible confusion with C++98.

## Executable probe

Run:

```sh
make cxx-eval
```

The probe compiles `programs/cxx_frontend_probe.cpp` for
`x86_64-none-elf` with Clang C++23, exceptions and RTTI disabled, and verifies
that the expected symbol is present in the ELF64 object. This validates the
frontend and target backend, not runtime support.

## Clang/LLVM

Advantages:

- already used by the CKLOS build and has a stable freestanding ELF64 backend;
- supports C++98 through C++23 language modes;
- Apache-2.0 WITH LLVM-exception is suitable for the project's MIT distribution.

R&D remaining:

1. Port libc++ and libc++abi to the CKLOS syscall ABI.
2. Supply static startup objects, thread-local storage and atomic support.
3. Implement C++ standard-library filesystem, locale and threading dependencies.
4. Integrate a driver in FS2 and test each `-std=c++NN` mode independently.
5. Decide whether exception tables and RTTI are required or build a no-exception
   profile first.

Estimate: 8-16 engineer-weeks for a no-exception static toolchain, and
12-24 engineer-weeks for broader conformance. A compiler binary, libc++ headers
and archives will be much larger than the current 1.44 MiB system floppy, so
they belong in the 128 MiB FS2 data disk and package repository.

## GCC

Advantages:

- mature C++ diagnostics and GNU extensions;
- broad conformance test coverage.

R&D remaining:

- maintain an `x86_64-cklos` GCC/binutils cross toolchain;
- port libstdc++, libgcc, TLS, unwinding and the debug/library ABI;
- preserve GPLv3 runtime and source-offer obligations;
- provide a reproducible bootstrap and update mechanism.

Estimate: 12-24 engineer-weeks for a static no-exception profile and
20-40 engineer-weeks for a broadly usable C++20/C++23 distribution.

## Decision

Clang/LLVM is the lower-risk first target because its frontend is already
available and the backend is proven by the executable probe. GCC remains a
separate long-term port, not an alias for `cck`. Neither frontend is promised
as a working C++ compiler by this evaluation.
