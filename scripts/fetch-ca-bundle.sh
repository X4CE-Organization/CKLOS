#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET="$ROOT/build/ca-certificates.crt"
URL="https://curl.se/ca/cacert.pem"
SHA256="796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe"

mkdir -p "$ROOT/build"
if [ ! -f "$TARGET" ] || \
    [ "$(shasum -a 256 "$TARGET" | awk '{print $1}')" != "$SHA256" ]; then
    curl -fsSL "$URL" -o "$TARGET"
fi
if [ "$(shasum -a 256 "$TARGET" | awk '{print $1}')" != "$SHA256" ]; then
    echo "CA bundle checksum mismatch" >&2
    exit 1
fi
echo "Mozilla CA bundle 2026-10-20 is ready."
