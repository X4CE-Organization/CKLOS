#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/third_party/musl"
PINNED_COMMIT=7813e9119eb5fe681b6750b8a033a0490bca0941

if [ ! -f "$TARGET/configure" ]; then
    git clone https://git.musl-libc.org/git/musl "$TARGET"
fi
git -C "$TARGET" checkout --detach "$PINNED_COMMIT"
echo "musl $PINNED_COMMIT is ready."
