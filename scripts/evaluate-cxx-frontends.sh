#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build"
LLVM_PREFIX=${LLVM_PREFIX:-$(brew --prefix llvm)}
CLANGXX=${CLANGXX:-$LLVM_PREFIX/bin/clang++}
LLVM_OBJDUMP=${LLVM_OBJDUMP:-$LLVM_PREFIX/bin/llvm-objdump}
LLVM_NM=${LLVM_NM:-$LLVM_PREFIX/bin/llvm-nm}

mkdir -p "$BUILD"

if [ ! -x "$CLANGXX" ]; then
    echo "Clang C++ frontend not found at $CLANGXX" >&2
    exit 1
fi

"$CLANGXX" --target=x86_64-none-elf -m64 -mcmodel=large \
    -std=c++23 -O2 -ffreestanding -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-stack-protector -fno-pic -fno-pie \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib \
    -c "$ROOT/programs/cxx_frontend_probe.cpp" \
    -o "$BUILD/cxx-frontend-probe.o"

"$LLVM_OBJDUMP" -f "$BUILD/cxx-frontend-probe.o"
"$LLVM_NM" "$BUILD/cxx-frontend-probe.o" |
    grep 'cklos_cxx_frontend_probe' >/dev/null

echo "Clang/LLVM C++23 freestanding frontend probe: PASS"
echo "Output: $BUILD/cxx-frontend-probe.o"
echo "This proves frontend code generation only; it does not provide libc++."

if command -v x86_64-elf-g++ >/dev/null 2>&1; then
    echo "GCC cross C++ frontend: $(command -v x86_64-elf-g++)"
else
    echo "GCC cross C++ frontend: not installed (evaluation only)"
fi
