#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/third_party/tinycc"
PINNED_COMMIT=e23dabed154a8495f3ac1ff3cc9d04fa1c207893

if [ ! -f "$TARGET/tcc.c" ]; then
    git clone https://github.com/TinyCC/tinycc.git "$TARGET"
fi
git -C "$TARGET" checkout --detach "$PINNED_COMMIT"
echo "TinyCC $PINNED_COMMIT is ready."
