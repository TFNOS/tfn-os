#!/usr/bin/env python3
"""Create per-device protection keys on the owner computer. No board access.

Requires cryptography. Choose a NEW private directory outside the repository,
then back it up before provisioning. Existing directories are never replaced.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def private_directory(directory):
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    if os.name == "nt":
        result = subprocess.run(
            ["whoami", "/user", "/fo", "csv", "/nh"], capture_output=True, text=True, check=True
        )
        sid = re.search(r"S-1-(?:\d+-)*\d+", result.stdout)
        if not sid:
            raise RuntimeError("Cannot identify the Windows owner; no keys written")
        subprocess.run(
            [
                "icacls",
                str(directory),
                "/inheritance:r",
                "/grant:r",
                "*" + sid[0] + ":(OI)(CI)F",
                "*S-1-5-18:(OI)(CI)F",
            ],
            capture_output=True,
            check=True,
        )
    else:
        directory.chmod(0o700)


def write_new(path, data):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags, 0o600), "wb") as output:
        output.write(data)


def generate(directory):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec, rsa

    directory = Path(directory).expanduser().absolute()
    private_directory(directory)
    public = {}
    fingerprints = {}
    for name in ("usb_admin", "app_sign", "firmware_sign", "recovery_sign"):
        key = (
            ec.generate_private_key(ec.SECP256R1())
            if name in ("usb_admin", "app_sign")
            else rsa.generate_private_key(public_exponent=65537, key_size=3072)
        )
        private = key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
        )
        pub = key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        write_new(directory / (name + ".pem"), private)
        write_new(directory / (name + ".pub.pem"), pub)
        fingerprints[name] = hashlib.sha256(pub).hexdigest()
        if name in ("usb_admin", "app_sign"):
            public[name] = key.public_key().public_bytes(
                serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
            )
    for name in ("flash_encryption.bin", "nvs_hmac.bin"):
        data = os.urandom(32)
        write_new(directory / name, data)
        fingerprints[name] = hashlib.sha256(data).hexdigest()
    header = "#pragma once\n#include <stdint.h>\n"
    for name, symbol in [("usb_admin", "DG_ADMIN_PUBKEY"), ("app_sign", "DG_APP_PUBKEY")]:
        header += "static const uint8_t " + symbol + "[65] = {\n"
        data = public[name]
        for offset in range(0, len(data), 13):
            header += "    " + ",".join("0x%02x" % b for b in data[offset : offset + 13]) + ",\n"
        header += "};\n"
    write_new(directory / "public_keys.h", header.encode("ascii"))
    write_new(directory / "fingerprints.json", (json.dumps(fingerprints, indent=2) + "\n").encode("ascii"))
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="New private directory outside Git")
    args = parser.parse_args()
    try:
        directory = generate(args.out)
    except FileExistsError:
        parser.exit(1, "Output already exists. Existing owner keys were preserved. Choose a new directory.\n")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(
            1,
            "Key creation stopped: "
            + str(error)
            + "\nPreserve any created files; do not overwrite existing keys.\n",
        )
    print("Created per-device keys in:", directory)
    print("Back up this private directory securely. Keys are unencrypted on this computer.")
    print("No board was connected, changed or enrolled. See sdk/docs/device-protection.md.")


if __name__ == "__main__":
    main()
