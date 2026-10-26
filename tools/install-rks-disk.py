#!/usr/bin/env python3
"""Install an RKS executable into an existing CKLOSFS1 disk image."""

import pathlib
import struct
import sys

FILE_MAGIC = 0x454C4946
DISK_VERSION = 1
MAX_FILES = 24
METADATA_START = 1
CONTENT_START = METADATA_START + MAX_FILES


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: install-rks-disk.py DISK.img FILE.RKS NAME.RKS", file=sys.stderr)
        return 2
    disk_path = pathlib.Path(sys.argv[1])
    content = pathlib.Path(sys.argv[2]).read_bytes()
    name = sys.argv[3].encode("ascii")
    if len(content) > 512:
        raise SystemExit("RKS file must fit in one 512-byte CKLOSFS file slot")
    if len(name) > 12:
        raise SystemExit("file name is too long")

    with disk_path.open("r+b") as disk:
        disk.seek(0)
        superblock = bytearray(disk.read(512))
        valid = bytes(superblock[:8]) == b"CKLOSFS1" and \
            struct.unpack_from("<I", superblock, 8)[0] == DISK_VERSION
        count = struct.unpack_from("<I", superblock, 12)[0] if valid else 0
        if count > MAX_FILES:
            count = 0

        slot = count
        found = False
        for index in range(count):
            disk.seek((METADATA_START + index) * 512)
            record = disk.read(512)
            magic, _, flags = struct.unpack_from("<III", record, 0)
            stored_name = bytes(record[16:28]).split(b"\0", 1)[0]
            if magic == FILE_MAGIC and flags and stored_name == name:
                slot = index
                found = True
                break

        if not found and count >= MAX_FILES:
            raise SystemExit("CKLOSFS disk is full")

        metadata = bytearray(512)
        content_sector = CONTENT_START + slot
        struct.pack_into("<IIII", metadata, 0, FILE_MAGIC, len(content),
                         0x80000001, content_sector)
        metadata[16:16 + len(name)] = name
        disk.seek((METADATA_START + slot) * 512)
        disk.write(metadata)

        payload = bytearray(512)
        payload[:len(content)] = content
        disk.seek(content_sector * 512)
        disk.write(payload)

        superblock[:8] = b"CKLOSFS1"
        struct.pack_into("<II", superblock, 8, DISK_VERSION,
                         count if found else count + 1)
        checksum = 2166136261
        for byte in superblock[:16]:
            checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
        struct.pack_into("<I", superblock, 16, checksum)
        disk.seek(0)
        disk.write(superblock)
    print(f"{disk_path}: {'updated' if found else 'installed'} {name.decode()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
