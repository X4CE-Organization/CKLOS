#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BEARSSL="$ROOT/third_party/bearssl"
MUSL_INCLUDE="$ROOT/build/musl-stage/usr/local/musl/include"
LLVM_PREFIX=${LLVM_PREFIX:-$(brew --prefix llvm)}
CLANG=${CLANG:-$LLVM_PREFIX/bin/clang}
LLVM_AR=${LLVM_AR:-$LLVM_PREFIX/bin/llvm-ar}

"$ROOT/scripts/fetch-crypto.sh"
"$ROOT/scripts/build-musl.sh" >/dev/null

make -C "$BEARSSL" -j4 BUILD=build-cklos CC="$CLANG" AR="$LLVM_AR" \
    CFLAGS="--target=x86_64-none-elf -m64 -mcmodel=large -Os -ffreestanding \
-fno-builtin -fno-stack-protector -fno-pic -fno-pie \
-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
-mno-sse -mno-sse2 -mno-mmx -mno-80387 -isystem $MUSL_INCLUDE" \
    build-cklos/libbearssl.a
echo "BearSSL freestanding client/server library is ready."
