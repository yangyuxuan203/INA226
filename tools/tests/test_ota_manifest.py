import hashlib
import struct
import unittest
import zlib

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from tools.ota_manifest import (
    APPLICATION_ADDRESS,
    FW_INFO_FORMAT_VERSION,
    FW_INFO_MAGIC,
    FW_INFO_OFFSET,
    FW_INFO_SIZE,
    OTA_BOARD_ID,
    OTA_METADATA_CRC_OFFSET,
    OTA_METADATA_FORMAT_VERSION,
    OTA_METADATA_MAGIC,
    OTA_METADATA_RECORD_SIZE,
    OTA_RESULT_NONE,
    OTA_STATE_CONFIRMED,
    SIGNING_DOMAIN,
    build_factory_metadata,
    build_payload,
    build_signing_message,
    read_firmware_info,
    validate_ota_url,
    vector_is_valid,
)


class OTAManifestTests(unittest.TestCase):
    def test_signing_message_has_fixed_binary_layout(self):
        image_hash = bytes(range(32))
        url = "https://example.test/app.bin"
        message = build_signing_message(
            OTA_BOARD_ID, 0x01020304, 0x00034567, 0x89ABCDEF, image_hash, url
        )
        self.assertEqual(
            message,
            SIGNING_DOMAIN
            + struct.pack(
                "<IIIII",
                OTA_BOARD_ID,
                0x01020304,
                0x00034567,
                0x89ABCDEF,
                len(url),
            )
            + image_hash
            + url.encode("ascii"),
        )

    def test_payload_signature_covers_all_download_metadata(self):
        image = struct.pack(
            "<II", 0x20020000, APPLICATION_ADDRESS + 0x101
        ) + bytes(range(128))
        private_key = Ed25519PrivateKey.generate()
        payload = build_payload(
            image, "https://example.test/app.bin", 7, OTA_BOARD_ID, private_key
        )
        params = payload["params"]
        self.assertEqual(payload["id"], "stm32f407-ota")
        self.assertEqual(payload["version"], "1.0")
        image_hash = bytes.fromhex(params["ota_hash"]["value"])
        image_crc32 = int(params["ota_crc32"]["value"], 0)
        url = params["ota_url"]["value"]
        signature = bytes.fromhex(params["ota_signature"]["value"])
        message = build_signing_message(
            OTA_BOARD_ID, 7, len(image), image_crc32, image_hash, url
        )

        public_key = private_key.public_key()
        public_key.verify(signature, message)

        tampered_messages = {
            "board": build_signing_message(
                OTA_BOARD_ID + 1, 7, len(image), image_crc32, image_hash, url
            ),
            "version": build_signing_message(
                OTA_BOARD_ID, 8, len(image), image_crc32, image_hash, url
            ),
            "size": build_signing_message(
                OTA_BOARD_ID, 7, len(image) + 1, image_crc32, image_hash, url
            ),
            "crc": build_signing_message(
                OTA_BOARD_ID, 7, len(image), image_crc32 ^ 1, image_hash, url
            ),
            "hash": build_signing_message(
                OTA_BOARD_ID,
                7,
                len(image),
                image_crc32,
                bytes([image_hash[0] ^ 1]) + image_hash[1:],
                url,
            ),
            "url": build_signing_message(
                OTA_BOARD_ID,
                7,
                len(image),
                image_crc32,
                image_hash,
                "https://mirror.example.test/app.bin",
            ),
        }
        for field, tampered_message in tampered_messages.items():
            with self.subTest(field=field), self.assertRaises(InvalidSignature):
                public_key.verify(signature, tampered_message)

    def test_factory_metadata_is_a_signed_confirmed_record(self):
        image = struct.pack(
            "<II", 0x20020000, APPLICATION_ADDRESS + 0x101
        ) + bytes(range(128))
        url = "https://example.test/factory.bin"
        private_key = Ed25519PrivateKey.generate()
        metadata = build_factory_metadata(
            image, url, 7, OTA_BOARD_ID, private_key
        )

        self.assertEqual(len(metadata), OTA_METADATA_RECORD_SIZE)
        fields = struct.unpack_from("<6Ii8I", metadata)
        self.assertEqual(fields[0], OTA_METADATA_MAGIC)
        self.assertEqual(fields[1], OTA_METADATA_FORMAT_VERSION)
        self.assertEqual(fields[2], OTA_METADATA_RECORD_SIZE)
        self.assertEqual(fields[3], 1)
        self.assertEqual(fields[4], OTA_STATE_CONFIRMED)
        self.assertEqual(fields[5], OTA_RESULT_NONE)
        self.assertEqual(fields[9], 7)
        self.assertEqual(fields[10], 7)
        self.assertEqual(fields[11], OTA_BOARD_ID)
        self.assertEqual(fields[12], len(image))
        self.assertEqual(fields[13], zlib.crc32(image) & 0xFFFFFFFF)
        self.assertEqual(fields[14], len(url))

        image_hash = hashlib.blake2b(image, digest_size=32).digest()
        signature = metadata[92:156]
        self.assertEqual(metadata[60:92], image_hash)
        self.assertEqual(metadata[156:156 + len(url)], url.encode("ascii"))
        private_key.public_key().verify(
            signature,
            build_signing_message(
                OTA_BOARD_ID,
                7,
                len(image),
                zlib.crc32(image) & 0xFFFFFFFF,
                image_hash,
                url,
            ),
        )
        self.assertEqual(
            struct.unpack_from("<I", metadata, OTA_METADATA_CRC_OFFSET)[0],
            zlib.crc32(metadata[4:OTA_METADATA_CRC_OFFSET]) & 0xFFFFFFFF,
        )

    def test_url_validation_rejects_http_and_at_injection(self):
        self.assertEqual(
            validate_ota_url("https://firmware.example.test/app.bin?channel=stable"),
            b"https://firmware.example.test/app.bin?channel=stable",
        )
        self.assertEqual(
            validate_ota_url("https://firmware.example.test?file=app.bin"),
            b"https://firmware.example.test?file=app.bin",
        )
        for url in (
            "https://example.test/app.bin\r\nAT+RST",
            'https://example.test"/app.bin',
            "https://user:secret@example.test/app.bin",
            "https://example.test/app.bin#fragment",
        ):
            with self.subTest(url=url), self.assertRaises(ValueError):
                validate_ota_url(url)

    def test_vector_validation_requires_relocated_thumb_handler(self):
        valid = struct.pack("<II", 0x20020000, APPLICATION_ADDRESS + 0x101)
        wrong_address = struct.pack("<II", 0x20020000, 0x08000101)
        arm_handler = struct.pack("<II", 0x20020000, APPLICATION_ADDRESS + 0x100)
        self.assertTrue(vector_is_valid(valid))
        self.assertFalse(vector_is_valid(wrong_address))
        self.assertFalse(vector_is_valid(arm_handler))

    def test_firmware_info_extracts_board_and_version(self):
        info = struct.pack(
            "<8I",
            FW_INFO_MAGIC,
            FW_INFO_FORMAT_VERSION,
            FW_INFO_SIZE,
            OTA_BOARD_ID,
            0x00010203,
            APPLICATION_ADDRESS,
            0,
            0,
        )
        image = bytes(FW_INFO_OFFSET) + info + bytes(64)
        self.assertEqual(read_firmware_info(image), (OTA_BOARD_ID, 0x00010203))
        with self.assertRaises(ValueError):
            read_firmware_info(bytes(FW_INFO_OFFSET) + bytes(FW_INFO_SIZE))


if __name__ == "__main__":
    unittest.main()
