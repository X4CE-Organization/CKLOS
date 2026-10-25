#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KEY_DIR="$ROOT/keys"
BUILD="$ROOT/build"

mkdir -p "$KEY_DIR" "$BUILD"

python3 - "$KEY_DIR" "$BUILD/keys.h" <<'PY'
import base64
import pathlib
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

key_dir = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
host_path = key_dir / "host_seed"
client_path = key_dir / "cklos_user"

if not host_path.exists():
    host_path.write_bytes(Ed25519PrivateKey.generate().private_bytes_raw())
if not client_path.exists():
    client = Ed25519PrivateKey.generate()
    client_path.write_bytes(client.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.OpenSSH,
        serialization.NoEncryption(),
    ))
    public = client.public_key().public_bytes(
        serialization.Encoding.OpenSSH,
        serialization.PublicFormat.OpenSSH,
    ) + b" cklos@localhost\n"
    (key_dir / "cklos_user.pub").write_bytes(public)

host_seed = host_path.read_bytes()
client_line = (key_dir / "cklos_user.pub").read_text().split()
client_public = base64.b64decode(client_line[1])[-32:]
assert len(host_seed) == 32
assert len(client_public) == 32

def array(name, data):
    body = ", ".join(f"0x{x:02x}" for x in data)
    return f"static const uint8_t {name}[32] = {{ {body} }};\n"

output.write_text(
    "#ifndef CKLOS_GENERATED_SSH_KEYS_H\n"
    "#define CKLOS_GENERATED_SSH_KEYS_H\n"
    + array("ssh_host_seed", host_seed)
    + array("ssh_authorized_public_key", client_public)
    + "#endif\n"
)
PY

chmod 600 "$KEY_DIR/host_seed" "$KEY_DIR/cklos_user"
echo "SSH development keys are ready in $KEY_DIR"
