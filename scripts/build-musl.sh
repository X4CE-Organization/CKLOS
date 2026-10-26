#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/third_party/musl"
BUILD="$ROOT/build/musl"
STAGE="$ROOT/build/musl-stage"
LLVM_PREFIX=${LLVM_PREFIX:-$(brew --prefix llvm 2>/dev/null || true)}
CLANG=${CLANG:-$LLVM_PREFIX/bin/clang}
LLVM_AR=${LLVM_AR:-$LLVM_PREFIX/bin/llvm-ar}
LLVM_RANLIB=${LLVM_RANLIB:-$LLVM_PREFIX/bin/llvm-ranlib}

test -x "$CLANG"
test -x "$LLVM_AR"
test -x "$LLVM_RANLIB"

"$ROOT/scripts/fetch-musl.sh"
if [ ! -f "$BUILD/configure" ]; then
    mkdir -p "$ROOT/build"
    cp -R "$SOURCE" "$BUILD"
fi

if [ ! -f "$BUILD/config.mak" ]; then
    (
        cd "$BUILD"
        CC="$CLANG" \
        CFLAGS="--target=x86_64-none-elf -m64 -mcmodel=large -std=c11 -Os \
-fno-pic -fno-pie -fno-stack-protector -ffreestanding -mno-red-zone \
-fno-asynchronous-unwind-tables -fno-unwind-tables" \
        LDFLAGS="-nostdlib -fuse-ld=lld" \
        ./configure --target=x86_64 --disable-shared --disable-pie \
            --disable-gcc-wrapper
    )
fi

# Replace the syscall instruction with CKLOS's stable int 0x80 ABI.
perl -pi -e 's/\bsyscall\b/int \$0x80/g' \
    "$BUILD/arch/x86_64/syscall_arch.h" \
    "$BUILD/src/thread/x86_64/__set_thread_area.s" \
    "$BUILD/src/thread/x86_64/syscall_cp.s" \
    "$BUILD/src/thread/x86_64/clone.s" \
    "$BUILD/src/thread/x86_64/__unmapself.s"

# Static images are linked at 0x4000000000 and have no dynamic linker.
perl -pi -e 's/"\tlea _DYNAMIC\(%rip\),%rsi \\n"/"\txor %rsi,%rsi \\n"/' \
    "$BUILD/arch/x86_64/crt_arch.h"

make -C "$BUILD" -j4 AR="$LLVM_AR" RANLIB="$LLVM_RANLIB"
make -C "$BUILD" install DESTDIR="$STAGE" AR="$LLVM_AR" RANLIB="$LLVM_RANLIB"
touch "$BUILD/.cklos-ready"
echo "musl static libc and headers are ready."
