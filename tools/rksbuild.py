#!/usr/bin/env python3
"""Package a flat x86-64 binary into the CKLOS RKS executable format."""

import pathlib
import struct
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: rksbuild.py INPUT.bin OUTPUT.RKS", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    code = source.read_bytes()
    if not code:
        raise SystemExit("RKS payload is empty")
    if len(code) > 0x100000:
        raise SystemExit("RKS payload exceeds 1 MiB")

    checksum = sum(code) & 0xFFFFFFFF
    header = struct.pack(
        "<4sHHIIIIII",
        b"RKS1",
        1,
        32,
        0,
        len(code),
        0,
        checksum,
        1,
        0,
    )
    image = header + code
    output.write_bytes(image)

    print(f"Built {output} ({len(image)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
