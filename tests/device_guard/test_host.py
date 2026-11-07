"""Exercise real host signing and failure handling against a synthetic peer."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import struct, zlib
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sdk/tools"))
from device_admin import Authorization, Maintenance, TransferError


class Peer:
    def __init__(self, key):
        self.key = key
        self.lines = []
        self.challenge = None
        self.counter = 0
        self.accepted = 0
        self.stream = bytearray()
        self.deny_once = False

    def write(self, data):
        command = data.strip()
        if command == b"admin challenge":
            self.counter += 1
            nonce = ("%064x" % self.counter).encode()
            mac = b"020000000001"
            self.challenge = b"TFNADMIN1:" + mac + b":" + nonce
            self.lines += [b"Other task log\n", b"ADMIN CHALLENGE " + mac + b" " + nonce + b"\n"]
        elif command.startswith(b"admin auth "):
            raw = bytes.fromhex(command[11:].decode())
            r = int.from_bytes(raw[:32], "big")
            s = int.from_bytes(raw[32:], "big")
            try:
                assert self.challenge is not None
                self.key.public_key().verify(
                    utils.encode_dss_signature(r, s), self.challenge, ec.ECDSA(hashes.SHA256())
                )
            except Exception:
                self.lines.append(b"ADMIN DENIED\n")
            else:
                self.lines.append(b"ADMIN OK\n")
                self.accepted += 1
            self.challenge = None
        elif command.startswith(b"maint "):
            _, identifier, op = command.split()
            assert op == b"ping"
            if self.deny_once:
                self.deny_once = False
                self.stream.extend(b"ADMIN REQUIRED\n")
            else:
                payload = b"P" + struct.pack("<H", 512)
                self.stream.extend(
                    b"USBFS1 "
                    + identifier
                    + b" "
                    + payload.hex().encode()
                    + b" "
                    + ("%08x" % zlib.crc32(payload)).encode()
                    + b"\n"
                )
        elif command == b"apps":
            self.lines.append(b"apps: simulated inventory\n")
        else:
            raise AssertionError("Unexpected request")

    def readline(self):
        return self.lines.pop(0) if self.lines else b""

    @property
    def in_waiting(self):
        return len(self.stream)

    def read(self, n):
        result = bytes(self.stream[:n])
        del self.stream[:n]
        return result


class HostTests(unittest.TestCase):
    def test_authentication_and_renewal(self):
        key = ec.generate_private_key(ec.SECP256R1())
        peer = Peer(key)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-key.pem"
            path.write_bytes(
                key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption(),
                )
            )
            auth = Authorization(peer, path)
            auth()
            self.assertEqual(peer.accepted, 1)
            auth()
            self.assertEqual(peer.accepted, 1)
            auth(force=True)
            self.assertEqual(peer.accepted, 2)
            wrong = Peer(ec.generate_private_key(ec.SECP256R1()))
            with self.assertRaises(TransferError):
                Authorization(wrong, path)()
            self.assertEqual(wrong.accepted, 0)

    def test_wrong_curve_is_rejected(self):
        key = ec.generate_private_key(ec.SECP384R1())
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-key.pem"
            path.write_bytes(
                key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption(),
                )
            )
            with self.assertRaises(TransferError):
                Authorization(Peer(key), path)

    def test_maintenance_renewal_and_console_command(self):
        key = ec.generate_private_key(ec.SECP256R1())
        peer = Peer(key)
        peer.deny_once = True
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-key.pem"
            path.write_bytes(
                key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption(),
                )
            )
            client = Maintenance(peer, path, timeout=0.05)
            self.assertEqual(client.chunk_size, 512)
            self.assertEqual(peer.accepted, 2)
            self.assertIn("apps: simulated inventory", client.command("apps", timeout=0.01))


if __name__ == "__main__":
    unittest.main()
