The OTA Ed25519 private key is generated here by:

    python tools/ota_keygen.py

The private PEM is ignored by Git. Back it up securely: losing it prevents new
firmware from being accepted by devices containing the corresponding public
key. Do not use the same development key for production devices.

The matching public key is written to bootloader/Inc/ota_public_key.h. Any key
rotation requires rebuilding and physically installing a Bootloader containing
the new public key before firmware signed by that key can be accepted.
