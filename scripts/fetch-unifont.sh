#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/third_party/unifont"
ARCHIVE="$TARGET/unifont-18.0.01.tar.gz"
BASE="$TARGET/unifont-base.hex"
CJK="$TARGET/wqy.hex"

mkdir -p "$TARGET"
if [ ! -f "$BASE" ] || [ ! -f "$CJK" ]; then
    curl -L --fail --silent --show-error \
        https://unifoundry.com/pub/unifont/unifont-18.0.01/unifont-18.0.01.tar.gz \
        -o "$ARCHIVE"
    tar -xOf "$ARCHIVE" unifont-18.0.01/font/plane00/unifont-base.hex > "$BASE"
    tar -xOf "$ARCHIVE" unifont-18.0.01/font/plane00/wqy.hex > "$CJK"
    tar -xOf "$ARCHIVE" unifont-18.0.01/COPYING > "$TARGET/COPYING"
fi

test -s "$BASE"
test -s "$CJK"
test -s "$TARGET/COPYING"
echo "GNU Unifont 18.0.01 source is ready."
