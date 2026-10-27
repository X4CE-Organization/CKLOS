#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build"

if [ ! -f "$BUILD/test-ca.der" ] || [ ! -f "$BUILD/test-key.pem" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$BUILD/test-key.pem" -out "$BUILD/test-cert.pem" \
        -days 3650 -subj '/CN=10.0.2.2' \
        -addext 'subjectAltName=DNS:10.0.2.2'
    openssl x509 -in "$BUILD/test-cert.pem" -outform DER \
        -out "$BUILD/test-ca.der"
fi
test -s "$BUILD/test-ca.der"
