#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VENDOR="$ROOT/third_party"

mkdir -p "$VENDOR"

if [ ! -f "$VENDOR/monocypher/src/monocypher.c" ]; then
    git clone --depth 1 --branch 4.0.2 \
        https://github.com/LoupVaillant/Monocypher.git \
        "$VENDOR/monocypher"
fi

if [ ! -f "$VENDOR/bearssl/src/inner.h" ]; then
    git clone --depth 1 --branch v0.6 \
        https://www.bearssl.org/git/BearSSL \
        "$VENDOR/bearssl"
fi

test -f "$VENDOR/monocypher/src/monocypher.c"
test -f "$VENDOR/bearssl/src/inner.h"
echo "Audited crypto dependencies are ready."
