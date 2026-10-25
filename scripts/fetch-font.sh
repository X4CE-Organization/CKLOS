#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/third_party/font8x8"

if [ ! -f "$TARGET/font8x8_basic.h" ]; then
    git clone --depth 1 https://github.com/dhepper/font8x8.git "$TARGET"
fi

test -f "$TARGET/font8x8_basic.h"
echo "Public-domain 8x8 font is ready."
