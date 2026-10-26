#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/third_party/tinycc"
BUILD="$ROOT/build/tinycc"
MUSL_STAGE="$ROOT/build/musl-stage/usr/local/musl"
STAGE="$ROOT/build/cklos-toolchain"
LLVM_PREFIX=${LLVM_PREFIX:-$(brew --prefix llvm)}
CLANG=${CLANG:-$LLVM_PREFIX/bin/clang}
LD_LLD=${LD_LLD:-$(brew --prefix lld)/bin/ld.lld}
LLVM_AR=${LLVM_AR:-$LLVM_PREFIX/bin/llvm-ar}
LLVM_RANLIB=${LLVM_RANLIB:-$LLVM_PREFIX/bin/llvm-ranlib}

"$ROOT/scripts/build-musl.sh"
"$ROOT/scripts/fetch-tinycc.sh"
if [ ! -f "$BUILD/configure" ]; then
    cp -R "$SOURCE" "$BUILD"
fi

if [ ! -f "$BUILD/config.mak" ]; then
    (
        cd "$BUILD"
        ./configure --cc="$CLANG" --cpu=x86_64 --targetos=Linux \
            --enable-cross --config-backtrace=no --prefix=/usr \
            --extra-cflags="--target=x86_64-none-elf -m64 -mcmodel=large \
-Os -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
-fno-unwind-tables -mno-red-zone -ffreestanding -nostdinc \
-isystem $MUSL_STAGE/include" \
            --extra-ldflags="-nostdlib -fuse-ld=lld"
    )
fi

# Deliver CKLOS branding and the standardized C mode matrix while keeping
# TinyCC's parser and LGPL source provenance clearly identified.
perl -pi -e '
    s/Tiny C Compiler "TCC_VERSION/CCK C compiler "TCC_VERSION/;
    s/Usage: tcc/Usage: cck/g;
    s/"tcc version "/"cck version "/;
    s/strcpy\(buf, "a\.out"\)/strcpy(buf, "a.rks")/;
' "$BUILD/tcc.c"
perl -0pi -e '
    s/if \(strcmp\(optarg, "=c11"\) == 0 \|\| strcmp\(optarg, "=gnu11"\) == 0\)\s*\n\s*s->cversion = 201112;/if (!strcmp(optarg, "=c89") || !strcmp(optarg, "=gnu89") || !strcmp(optarg, "=c90") || !strcmp(optarg, "=gnu90"))\n                s->cversion = 199409;\n            else if (!strcmp(optarg, "=c99") || !strcmp(optarg, "=gnu99"))\n                s->cversion = 199901;\n            else if (!strcmp(optarg, "=c11") || !strcmp(optarg, "=gnu11"))\n                s->cversion = 201112;\n            else if (!strcmp(optarg, "=c17") || !strcmp(optarg, "=gnu17"))\n                s->cversion = 201710;\n            else if (!strcmp(optarg, "=c23") || !strcmp(optarg, "=gnu23"))\n                s->cversion = 202311;/s;
' "$BUILD/libtcc.c"
perl -pi -e 's/tcc:/cck:/g; s/"tcc: /"cck: /g' "$BUILD/libtcc.c"
touch "$BUILD/tcc.c" "$BUILD/libtcc.c"

make -C "$BUILD" tcc.o libtcc.a AR="$LLVM_AR" RANLIB="$LLVM_RANLIB"
make -C "$BUILD/lib" CROSS_TARGET=x86_64 x86_64-libtcc1-usegcc=yes \
    CC="$CLANG" AR="$LLVM_AR"

mkdir -p "$STAGE/bin" "$STAGE/lib/tcc"
"$LD_LLD" -m elf_x86_64 -T "$ROOT/programs/user.ld" --build-id=none \
    --gc-sections -o "$STAGE/bin/cck" \
    "$MUSL_STAGE/lib/crt1.o" "$BUILD/tcc.o" "$BUILD/libtcc.a" \
    "$MUSL_STAGE/lib/libc.a"
cp "$BUILD/x86_64-libtcc1.a" "$STAGE/lib/tcc/libtcc1.a"
cp -R "$BUILD/include" "$STAGE/lib/tcc/include"
touch "$BUILD/.cklos-ready"
echo "cck and TinyCC runtime files are ready."
