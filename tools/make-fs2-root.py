#!/usr/bin/env python3
"""Create the factory directory tree for a CKLOSFS2 image."""

import pathlib
import shutil
import sys

root = pathlib.Path(sys.argv[1])
hello = pathlib.Path(sys.argv[2])

if root.exists():
    shutil.rmtree(root)
for path in ("bin", "etc", "home/cklos", "import", "usr/include", "usr/lib"):
    (root / path).mkdir(parents=True, exist_ok=True)

(root / "MOTD.TXT").write_text(
    "Welcome to CKLOS.\n"
    "CKLOSFS2 is a directory-based variable-length filesystem.\n"
    "Use FSLS, FSCD, FSWRITE and FSCAT to browse it.\n"
)
(root / "ABOUT.TXT").write_text(
    "CKLOS 0.8 filesystem foundation.\n"
    "CKLOSFS2 uses LittleFS metadata for power-loss-safe updates.\n"
)
(root / "LICENSE.TXT").write_text("CKLOS is distributed under the MIT License.\n")
(root / "DEMO.CKL").write_text(
    "# CKLOS script engine demonstration\n"
    "echo CKLOS script engine is active.\n"
    "uptime\n"
    "df\n"
)
shutil.copy2(hello, root / "bin" / "HELLO.RKS")
