#!/usr/bin/env python3
"""Create or load the local OTA Ed25519 key and export its public C header."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError as exc:  # pragma: no cover - exercised only without dependency
    raise SystemExit(
        "cryptography is required; run: python -m pip install -r tools/requirements-ota.txt"
    ) from exc


TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
DEFAULT_PRIVATE_KEY = TOOLS_DIR / "config" / "ota_ed25519_private.pem"
DEFAULT_PUBLIC_HEADER = REPO_ROOT / "bootloader" / "Inc" / "ota_public_key.h"


def load_private_key(path: Path) -> Ed25519PrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError(f"private key is not Ed25519: {path}")
    return key


def public_key_bytes(key: Ed25519PrivateKey) -> bytes:
    return key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )


def render_public_header(public_key: bytes) -> str:
    if len(public_key) != 32:
        raise ValueError("Ed25519 public key must be 32 bytes")
    rows = []
    for offset in range(0, len(public_key), 8):
        row = ", ".join(f"0x{byte:02X}U" for byte in public_key[offset : offset + 8])
        rows.append(f"    {row},")
    fingerprint = hashlib.sha256(public_key).hexdigest()
    return (
        "#ifndef OTA_PUBLIC_KEY_H\n"
        "#define OTA_PUBLIC_KEY_H\n\n"
        "#include <stdint.h>\n\n"
        "#define OTA_ED25519_PUBLIC_KEY_SIZE 32U\n"
        f"#define OTA_ED25519_PUBLIC_KEY_SHA256 \"{fingerprint}\"\n\n"
        "static const uint8_t OTA_ED25519_PUBLIC_KEY[OTA_ED25519_PUBLIC_KEY_SIZE] = {\n"
        + "\n".join(rows)
        + "\n};\n\n"
        "#endif /* OTA_PUBLIC_KEY_H */\n"
    )


def save_private_key(path: Path, key: Ed25519PrivateKey) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    path.write_bytes(pem)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a local Ed25519 OTA signing key and public C header."
    )
    parser.add_argument("--private-key", type=Path, default=DEFAULT_PRIVATE_KEY)
    parser.add_argument("--public-header", type=Path, default=DEFAULT_PUBLIC_HEADER)
    parser.add_argument(
        "--rotate",
        action="store_true",
        help="Replace an existing private key. Previously signed firmware will no longer verify.",
    )
    args = parser.parse_args()

    if args.private_key.exists() and not args.rotate:
        key = load_private_key(args.private_key)
        action = "loaded"
    else:
        key = Ed25519PrivateKey.generate()
        save_private_key(args.private_key, key)
        action = "generated"

    public_key = public_key_bytes(key)
    args.public_header.parent.mkdir(parents=True, exist_ok=True)
    args.public_header.write_text(render_public_header(public_key), encoding="ascii")

    print(f"private key {action}: {args.private_key}")
    print(f"public header written: {args.public_header}")
    print(f"public key SHA-256: {hashlib.sha256(public_key).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
