# RKS1 Executable Format

RKS1 is CKLOS's native executable format. It contains checksummed flat x86-64 code and is loaded at `0x00300000`.

## Header

All integers are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic, `RKS1` |
| 4 | 2 | Format version, currently `1` |
| 6 | 2 | Header size, currently `32` |
| 8 | 4 | Entry offset relative to load base |
| 12 | 4 | Code size |
| 16 | 4 | Data size |
| 20 | 4 | Additive 32-bit payload checksum |
| 24 | 4 | Flags, bit 0 means executable |
| 28 | 4 | Reserved |

The executable entry uses this ABI:

```c
int rks_main(const rks_api *api);
```

The API table supplies text output, clearing, PIT ticks, millisecond sleep and mouse coordinates. Programs currently execute in ring 0 and must return normally.

`tools/rksbuild.py` creates RKS1 files. `tools/install-rks-disk.py` installs them into the CKLOSFS data disk without embedding them in the kernel.
