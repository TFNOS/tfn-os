"""Check generated trust material, independent identities and overwrite refusal."""

from pathlib import Path
import json
import os
import re
import sys
import tempfile
import unittest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/tools"))
from device_keys import generate


class KeyTests(unittest.TestCase):
    def test_new_identity_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = generate(Path(tmp) / "first")
            before = {p.name: p.read_bytes() for p in directory.iterdir()}
            self.assertEqual(len(before), 12)
            with self.assertRaises(FileExistsError):
                generate(directory)
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir()})
            header = before["public_keys.h"].decode()
            for name, symbol in [("usb_admin", "DG_ADMIN_PUBKEY"), ("app_sign", "DG_APP_PUBKEY")]:
                key = serialization.load_pem_private_key(before[name + ".pem"], None)
                pub = serialization.load_pem_public_key(before[name + ".pub.pem"])
                self.assertIsInstance(key.curve, ec.SECP256R1)
                signature = key.sign(b"owner challenge", ec.ECDSA(hashes.SHA256()))
                pub.verify(signature, b"owner challenge", ec.ECDSA(hashes.SHA256()))
                encoded = re.search(symbol + r"\[65\] = \{(.*?)\};", header, re.S)[1]
                raw = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", encoded))
                self.assertEqual(
                    raw,
                    pub.public_bytes(
                        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
                    ),
                )
            for name in ("firmware_sign", "recovery_sign"):
                key = serialization.load_pem_private_key(before[name + ".pem"], None)
                pub = serialization.load_pem_public_key(before[name + ".pub.pem"])
                self.assertEqual(key.key_size, 3072)
                signature = key.sign(
                    b"firmware",
                    padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                    hashes.SHA256(),
                )
                pub.verify(
                    signature,
                    b"firmware",
                    padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                    hashes.SHA256(),
                )
            self.assertEqual(len(before["flash_encryption.bin"]), 32)
            self.assertEqual(len(before["nvs_hmac.bin"]), 32)
            fingerprints = json.loads(before["fingerprints.json"])
            self.assertEqual(len(fingerprints), 6)
            if os.name != "nt":
                self.assertEqual(directory.stat().st_mode & 0o777, 0o700)
                self.assertTrue(all(p.stat().st_mode & 0o777 == 0o600 for p in directory.iterdir()))
            second = generate(Path(tmp) / "second")
            for name in (
                "usb_admin.pem",
                "app_sign.pem",
                "firmware_sign.pem",
                "recovery_sign.pem",
                "flash_encryption.bin",
                "nvs_hmac.bin",
            ):
                self.assertNotEqual(before[name], (second / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
