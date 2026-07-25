import hashlib
import re
import unittest

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from tools.ota_keygen import public_key_bytes, render_public_header


class OTAKeygenTests(unittest.TestCase):
    def test_public_header_contains_only_matching_public_key(self):
        private_key = Ed25519PrivateKey.generate()
        expected_public_key = public_key_bytes(private_key)
        header = render_public_header(expected_public_key)
        encoded_bytes = bytes(
            int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})U", header)
        )

        self.assertEqual(encoded_bytes, expected_public_key)
        self.assertIn(hashlib.sha256(expected_public_key).hexdigest(), header)
        self.assertNotIn("PRIVATE KEY", header)


if __name__ == "__main__":
    unittest.main()
