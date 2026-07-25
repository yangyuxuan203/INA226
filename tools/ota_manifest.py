#!/usr/bin/env python3
"""Create a signed OneNET property-set payload for an STM32F407 OTA image."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys
import urllib.parse
import zlib

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError as exc:  # pragma: no cover - exercised only without dependency
    raise SystemExit(
        "cryptography is required; run: python -m pip install -r tools/requirements-ota.txt"
    ) from exc


APPLICATION_ADDRESS = 0x08020000
APPLICATION_SIZE = 384 * 1024
OTA_BOARD_ID = 0xF4070001
OTA_METADATA_MAGIC = 0x4F544133
OTA_METADATA_FORMAT_VERSION = 3
OTA_METADATA_RECORD_SIZE = 512
OTA_METADATA_CRC_OFFSET = 508
OTA_STATE_CONFIRMED = 1
OTA_RESULT_NONE = 0
FW_INFO_MAGIC = 0x46575632
FW_INFO_FORMAT_VERSION = 1
FW_INFO_SIZE = 32
FW_INFO_OFFSET = 0x200
SIGNING_DOMAIN = b"STM32F407-OTA-V3"
TOOLS_DIR = pathlib.Path(__file__).resolve().parent
DEFAULT_PRIVATE_KEY = TOOLS_DIR / "config" / "ota_ed25519_private.pem"
DEFAULT_REQUEST_ID = "stm32f407-ota"
REQUEST_ID_MAX_LENGTH = 47
SRAM_RANGES = (
    (0x20000000, 0x20020000),
    (0x10000000, 0x10010000),
)


def parse_u32(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit in uint32")
    return value


def vector_is_valid(image: bytes) -> bool:
    if len(image) < 8:
        return False
    initial_sp, reset_handler = struct.unpack_from("<II", image)
    stack_valid = any(start <= initial_sp <= end for start, end in SRAM_RANGES)
    reset_address = reset_handler & ~1
    reset_valid = (
        (reset_handler & 1) != 0
        and APPLICATION_ADDRESS <= reset_address < APPLICATION_ADDRESS + APPLICATION_SIZE
    )
    return stack_valid and reset_valid


def read_firmware_info(image: bytes) -> tuple[int, int]:
    if len(image) < FW_INFO_OFFSET + FW_INFO_SIZE:
        raise ValueError("binary does not contain the fixed FWV2 version block")

    fields = struct.unpack_from("<8I", image, FW_INFO_OFFSET)
    magic, format_version, info_size, board_id, version, app_address, _, _ = fields
    if (
        magic != FW_INFO_MAGIC
        or format_version != FW_INFO_FORMAT_VERSION
        or info_size != FW_INFO_SIZE
        or app_address != APPLICATION_ADDRESS
        or board_id == 0
        or version == 0
    ):
        raise ValueError("binary contains an invalid fixed FWV2 version block")
    return board_id, version


def build_signing_message(
    board_id: int,
    image_version: int,
    image_size: int,
    image_crc32: int,
    image_hash: bytes,
    url: str,
) -> bytes:
    if len(image_hash) != 32:
        raise ValueError("BLAKE2b-256 hash must be 32 bytes")
    url_bytes = validate_ota_url(url)
    return (
        SIGNING_DOMAIN
        + struct.pack(
            "<IIIII",
            board_id,
            image_version,
            image_size,
            image_crc32,
            len(url_bytes),
        )
        + image_hash
        + url_bytes
    )


def validate_ota_url(url: str) -> bytes:
    try:
        url_bytes = url.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("URL must be ASCII; percent-encode non-ASCII characters") from exc

    if not 0 < len(url_bytes) < 256:
        raise ValueError("URL must contain 1..255 ASCII characters")
    if any(byte < 0x21 or byte > 0x7E or byte in (ord('"'), ord("\\"))
           for byte in url_bytes):
        raise ValueError("URL contains whitespace, control, quote, or backslash characters")

    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError("URL must be an absolute http:// or https:// URL")
    if parsed.username is not None or parsed.password is not None or parsed.fragment:
        raise ValueError("URL must not contain credentials or a fragment")
    if re.fullmatch(r"[A-Za-z0-9.-]+", parsed.hostname) is None:
        raise ValueError("URL host must be a DNS name or IPv4 address")
    if len(parsed.hostname) >= 96:
        raise ValueError("URL host must contain at most 95 characters")
    if parsed.hostname[0] in ".-" or parsed.hostname[-1] in ".-":
        raise ValueError("URL host must start and end with a letter or digit")
    try:
        port = parsed.port
    except ValueError as exc:
        raise ValueError("URL contains an invalid port") from exc
    if port == 0:
        raise ValueError("URL port must be in the range 1..65535")
    return url_bytes


def load_private_key(path: pathlib.Path) -> Ed25519PrivateKey:
    if not path.is_file():
        raise FileNotFoundError(
            f"OTA private key not found: {path}; run python tools/ota_keygen.py"
        )
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError(f"OTA private key is not Ed25519: {path}")
    return key


def sign_image_manifest(
    image: bytes,
    url: str,
    image_version: int,
    board_id: int,
    private_key: Ed25519PrivateKey,
) -> tuple[int, bytes, bytes]:
    image_hash = hashlib.blake2b(image, digest_size=32).digest()
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    signing_message = build_signing_message(
        board_id, image_version, len(image), crc32, image_hash, url
    )
    signature = private_key.sign(signing_message)

    try:
        private_key.public_key().verify(signature, signing_message)
    except InvalidSignature as exc:  # pragma: no cover - defensive check
        raise RuntimeError("generated OTA signature failed self-verification") from exc
    return crc32, image_hash, signature


def build_payload(
    image: bytes,
    url: str,
    image_version: int,
    board_id: int,
    private_key: Ed25519PrivateKey,
    request_id: str = DEFAULT_REQUEST_ID,
) -> dict[str, object]:
    crc32, image_hash, signature = sign_image_manifest(
        image, url, image_version, board_id, private_key
    )

    return {
        "id": request_id,
        "version": "1.0",
        "params": {
            "ota_upgrade": {"value": 1},
            "ota_url": {"value": url},
            "ota_size": {"value": len(image)},
            "ota_crc32": {"value": f"0x{crc32:08X}"},
            "ota_version": {"value": image_version},
            "ota_board_id": {"value": f"0x{board_id:08X}"},
            "ota_hash": {"value": image_hash.hex()},
            "ota_signature": {"value": signature.hex()},
        }
    }


def build_factory_metadata(
    image: bytes,
    url: str,
    image_version: int,
    board_id: int,
    private_key: Ed25519PrivateKey,
) -> bytes:
    url_bytes = validate_ota_url(url)
    crc32, image_hash, signature = sign_image_manifest(
        image, url, image_version, board_id, private_key
    )
    record = bytearray(OTA_METADATA_RECORD_SIZE)

    struct.pack_into(
        "<6Ii8I",
        record,
        0,
        OTA_METADATA_MAGIC,
        OTA_METADATA_FORMAT_VERSION,
        OTA_METADATA_RECORD_SIZE,
        1,
        OTA_STATE_CONFIRMED,
        OTA_RESULT_NONE,
        0,
        0,
        0,
        image_version,
        image_version,
        board_id,
        len(image),
        crc32,
        len(url_bytes),
    )
    record[60:92] = image_hash
    record[92:156] = signature
    record[156:156 + len(url_bytes)] = url_bytes
    metadata_crc32 = zlib.crc32(record[4:OTA_METADATA_CRC_OFFSET]) & 0xFFFFFFFF
    struct.pack_into("<I", record, OTA_METADATA_CRC_OFFSET, metadata_crc32)
    return bytes(record)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a signed OneNET property-set payload for an application binary."
    )
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("url")
    parser.add_argument("version", type=parse_u32)
    parser.add_argument("--board-id", type=parse_u32, default=OTA_BOARD_ID)
    parser.add_argument("--private-key", type=pathlib.Path, default=DEFAULT_PRIVATE_KEY)
    parser.add_argument("--request-id", default=DEFAULT_REQUEST_ID)
    parser.add_argument(
        "--factory-metadata-output",
        type=pathlib.Path,
        help="also write a signed 512-byte factory metadata image for 0x08010000",
    )
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    try:
        image = args.binary.read_bytes()
        private_key = load_private_key(args.private_key)
        embedded_board_id, embedded_version = read_firmware_info(image)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if len(image) > APPLICATION_SIZE:
        print(
            f"error: image is {len(image)} bytes; maximum is {APPLICATION_SIZE} bytes",
            file=sys.stderr,
        )
        return 1
    if args.version == 0:
        print("error: version must be in the range 1..4294967295", file=sys.stderr)
        return 1
    if args.board_id == 0:
        print("error: board-id must be non-zero", file=sys.stderr)
        return 1
    if embedded_board_id != args.board_id:
        print(
            f"error: binary board-id 0x{embedded_board_id:08X} does not match "
            f"requested 0x{args.board_id:08X}",
            file=sys.stderr,
        )
        return 1
    if embedded_version != args.version:
        print(
            f"error: binary version 0x{embedded_version:08X} does not match "
            f"requested 0x{args.version:08X}",
            file=sys.stderr,
        )
        return 1
    if not args.request_id or len(args.request_id) > REQUEST_ID_MAX_LENGTH:
        print(
            f"error: request-id must contain 1..{REQUEST_ID_MAX_LENGTH} characters",
            file=sys.stderr,
        )
        return 1
    try:
        validate_ota_url(args.url)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if not vector_is_valid(image):
        print(
            "error: binary vector table is not linked for application address 0x08020000",
            file=sys.stderr,
        )
        return 1

    payload = build_payload(
        image, args.url, args.version, args.board_id, private_key, args.request_id
    )
    if args.factory_metadata_output is not None:
        try:
            factory_metadata = build_factory_metadata(
                image, args.url, args.version, args.board_id, private_key
            )
            args.factory_metadata_output.parent.mkdir(parents=True, exist_ok=True)
            args.factory_metadata_output.write_bytes(factory_metadata)
        except OSError as exc:
            print(f"error: cannot write factory metadata: {exc}", file=sys.stderr)
            return 1
    if args.pretty:
        print(json.dumps(payload, ensure_ascii=True, indent=2))
    else:
        print(json.dumps(payload, ensure_ascii=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
