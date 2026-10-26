#!/usr/bin/env python3
"""Build the CKLOS 16x16 Unicode glyph table from GNU Unifont."""

import pathlib
import struct
import sys

output = pathlib.Path(sys.argv[1])
sources = [pathlib.Path(path) for path in sys.argv[2:]]
glyphs = {}

for source in sources:
    with source.open("r", encoding="ascii", errors="strict") as stream:
        for line in stream:
            code_text, _, bitmap = line.strip().partition(":")
            if not code_text or len(bitmap) != 64:
                continue
            codepoint = int(code_text, 16)
            if codepoint < 0x80:
                continue
            glyphs[codepoint] = bytes.fromhex(bitmap)

with output.open("wb") as stream:
    stream.write(b"UF16")
    stream.write(struct.pack("<I", len(glyphs)))
    for codepoint in sorted(glyphs):
        stream.write(struct.pack("<I", codepoint))
        stream.write(glyphs[codepoint])

print(f"Built {output} with {len(glyphs)} Unicode glyphs")
