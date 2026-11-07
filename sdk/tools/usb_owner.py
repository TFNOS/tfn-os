"""Owner authentication for a USB peer that already supports signed challenges.

The private P-256 key stays on the host. Importing this module does not connect
to a board or enable protection. Ordinary sd_usb use does not import it.
"""
from pathlib import Path
import re
import time
from sd_usb import TransferError


class Authorization:
    def __init__(self, port, key):
        try:
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric import ec
        except ImportError as error:
            raise TransferError('--admin-key needs the Python cryptography package') from error
        self.port = port
        try:
            self.key = serialization.load_pem_private_key(Path(key).read_bytes(), None)
        except (ValueError, TypeError) as error:
            raise TransferError('Use an unencrypted owner P-256 PEM private key') from error
        if not isinstance(self.key, ec.EllipticCurvePrivateKey) or not isinstance(self.key.curve, ec.SECP256R1):
            raise TransferError('USB authorization requires an owner P-256 key')
        self.last = None
        self.mac = None

    def invalidate(self):
        self.last = None

    def __call__(self, force=False):
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec, utils
        if not force and self.last is not None and time.monotonic() - self.last < 30:
            return
        self.port.write(b'admin challenge\r\n')
        deadline = time.monotonic() + 5
        challenge = None
        while time.monotonic() < deadline:
            line = self.port.readline()
            match = re.search(rb'ADMIN CHALLENGE ([0-9a-f]{12}) ([0-9a-f]{64})', line)
            if match:
                challenge = match.groups()
                break
            if b'ADMIN WAIT' in line:
                raise TransferError('Device authorization is temporarily rate limited')
        if challenge is None:
            raise TransferError('No owner challenge from the board')
        message = b'TFNADMIN1:' + challenge[0] + b':' + challenge[1]
        der = self.key.sign(message, ec.ECDSA(hashes.SHA256()))
        r, s = utils.decode_dss_signature(der)
        signature = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
        self.port.write(b'admin auth ' + signature.hex().encode('ascii') + b'\r\n')
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            line = self.port.readline()
            if b'ADMIN OK' in line:
                self.last = time.monotonic()
                self.mac = challenge[0].decode('ascii')
                return
            if b'ADMIN DENIED' in line:
                raise TransferError('The board does not trust this administration key')
        raise TransferError('No authorization acknowledgment')
