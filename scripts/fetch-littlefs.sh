#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/third_party/littlefs"
PINNED_COMMIT=6cb4e86540eca0d9ba62500a298385c9d863c8be

if [ ! -f "$TARGET/lfs.c" ]; then
    git clone --depth 1 --branch v2.11.3 \
        https://github.com/littlefs-project/littlefs.git "$TARGET"
fi

test -f "$TARGET/lfs.c"
actual=$(git -C "$TARGET" rev-parse HEAD)
if [ "$actual" != "$PINNED_COMMIT" ]; then
    echo "LittleFS commit mismatch: expected $PINNED_COMMIT, got $actual" >&2
    exit 1
fi
echo "LittleFS 2.11.3 is ready."
