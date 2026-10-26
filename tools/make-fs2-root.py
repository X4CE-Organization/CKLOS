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
executables = []
musl_root = None
license = None
arguments = sys.argv[3:]
index = 0
while index < len(arguments):
    argument = arguments[index]
    if argument == "--musl":
        index += 1
        musl_root = pathlib.Path(arguments[index])
    elif argument == "--license":
        index += 1
        license = pathlib.Path(arguments[index])
    else:
        executables.append(pathlib.Path(argument))
    index += 1

for executable in executables:
    shutil.copy2(executable, root / "bin" / executable.name)

if musl_root is not None:
    include_target = root / "usr" / "include"
    library_target = root / "usr" / "lib"
    for item in (musl_root / "include").iterdir():
        target = include_target / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)
    for item in (musl_root / "lib").iterdir():
        target = library_target / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)

if license is not None:
    license_target = root / "usr" / "share" / "licenses" / "musl"
    license_target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(license, license_target / "COPYRIGHT")
