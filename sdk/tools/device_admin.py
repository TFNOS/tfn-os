#!/usr/bin/env python3
"""Owner-authenticated USB maintenance. Private keys stay on the owner's PC.

Examples:
  device_admin.py --port COM4 --key usb_admin.pem command apps
  device_admin.py --port COM4 --key usb_admin.pem ls /data
  device_admin.py --port COM4 --key usb_admin.pem get /data/home.txt --out home.txt
  device_admin.py --port COM4 --key usb_admin.pem install package/apps/lanscan --backup backups
  device_admin.py --port COM4 --key usb_admin.pem update signed-firmware.bin
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import time
import zlib
from sd_usb import Client, TransferError, remote_path, local_name

from usb_owner import Authorization


class Maintenance(Client):
    def __init__(self, port, key, timeout=8):
        self.authorization = Authorization(port, key)
        super().__init__(port, timeout, command="maint", authorize=self.authorization)

    @staticmethod
    def ack(reply, expected=None):
        if len(reply) != 9 or reply[:1] != b"A":
            raise TransferError("Invalid maintenance acknowledgment")
        value = struct.unpack("<Q", reply[1:])[0]
        if expected is not None and value != expected:
            raise TransferError(f"Unexpected acknowledged offset: {value}, expected {expected}")
        return value

    def command(self, text, timeout=3):
        if "\n" in text or "\r" in text:
            raise TransferError("One console command per call")
        self.authorization(force=True)
        self.port.write(text.encode("utf-8") + b"\r\n")
        end = time.monotonic() + timeout
        out = []
        while time.monotonic() < end:
            line = self.port.readline()
            if line:
                out.append(line.decode("utf-8", "replace").rstrip())
        return out

    def mkdir(self, path):
        self.ack(self.request("mkdir", remote_path(path)))

    def put(self, path, data):
        path = remote_path(path)
        if not data:
            raise TransferError("Empty upload not supported")
        for offset in range(0, len(data), self.chunk_size):
            chunk = data[offset : offset + self.chunk_size]
            self.ack(self.request("put", path, offset, chunk.hex()), offset + len(chunk))
        size, crc = self.info(path)
        if size != len(data) or crc != zlib.crc32(data):
            raise TransferError("Uploaded file failed readback verification")

    def install(self, package, backup, sign_key=None):
        package = Path(package)
        manifest = (package / "manifest.txt").read_bytes()
        fields = dict(line.split("=", 1) for line in manifest.decode().splitlines() if "=" in line)
        appid = fields.get("id", "")
        if not re.fullmatch(r"[a-z0-9_]{1,20}", appid):
            raise TransferError("Invalid app ID")
        names = ["app.elf", "icon.bin", "manifest.txt", "sig"]
        if fields.get("watch_sha256"):
            names.append("watch.elf")
        data = {name: (package / name).read_bytes() for name in names if name != "sig"}
        if sign_key:
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import ec, utils

            signing = serialization.load_pem_private_key(Path(sign_key).read_bytes(), None)
            if not isinstance(signing, ec.EllipticCurvePrivateKey) or not isinstance(
                signing.curve, ec.SECP256R1
            ):
                raise TransferError("App signing requires a P-256 key")
            r, s = utils.decode_dss_signature(signing.sign(manifest, ec.ECDSA(hashes.SHA256())))
            data["sig"] = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        else:
            data["sig"] = (package / "sig").read_bytes()
        if len(data["sig"]) != 64:
            raise TransferError("Package must be signed before installation")
        for name, key in [
            ("app.elf", "elf_sha256"),
            ("icon.bin", "icon_sha256"),
            ("watch.elf", "watch_sha256"),
        ]:
            if name in data and hashlib.sha256(data[name]).hexdigest() != fields.get(key):
                raise TransferError("Package hash mismatch: " + name)
        if len(data["app.elf"]) != int(fields.get("bytes", "-1")):
            raise TransferError("ELF size does not match manifest")
        remote = "/data/apps/" + appid
        backup = Path(backup) / appid
        if backup.exists():
            raise TransferError("Choose a new backup directory; existing backups are preserved")
        backup.mkdir(parents=True)
        # Close the active app before replacing any package files.
        self.command("open MENU", timeout=0.5)
        try:
            old = list(self.entries(remote))
        except TransferError as e:
            if "NOT_DIRECTORY" not in str(e):
                raise
            old = []
        for entry in old:
            if entry.directory:
                raise TransferError("Unexpected subdirectory in installed package")
            self.download(remote + "/" + entry.name, backup / local_name(entry.name))
        stage = "/data/owner-upload/" + appid
        self.mkdir("/data/owner-upload")
        self.mkdir(stage)
        # The native installer verifies signature, ABI and hashes before it
        # replaces a live package, preserving the old app on validation failure.
        for name, value in data.items():
            self.put(stage + "/" + name, value)
        self.timeout = 30
        self.ack(self.request("install", stage))
        self.timeout = 8
        print("\n".join(self.command("apps scan", timeout=2)))

    def update(self, image):
        data = Path(image).read_bytes()
        if len(data) < 65536 or data[0] != 0xE9:
            raise TransferError("Not a firmware image")
        self.timeout = 60
        self.ack(self.request("fw_begin", None, len(data), hashlib.sha256(data).hexdigest()), len(data))
        self.timeout = 8
        try:
            for offset in range(0, len(data), self.chunk_size):
                chunk = data[offset : offset + self.chunk_size]
                self.ack(self.request("fw_write", None, offset, chunk.hex()), offset + len(chunk))
                if offset % (256 * 1024) == 0:
                    print("Firmware sent:", offset, "/", len(data), flush=True)
            self.timeout = 30
            self.ack(self.request("fw_finish"))
        except BaseException:
            try:
                self.request("fw_abort")
            except (OSError, TransferError):
                pass
            raise
        print("Signed firmware accepted; board is restarting", flush=True)

    def flash_backup(self, target):
        target = Path(target)
        if target.exists() or target.with_suffix(target.suffix + ".part").exists():
            raise TransferError("Backup target already exists")
        reply = self.request("flash_info")
        if len(reply) != 9 or reply[:1] != b"Z":
            raise TransferError("Invalid flash size reply")
        size = struct.unpack("<Q", reply[1:])[0]
        if not 0 < size <= 32 * 1024 * 1024:
            raise TransferError("Unexpected flash size")
        target.parent.mkdir(parents=True, exist_ok=True)
        part = target.with_suffix(target.suffix + ".part")
        sha = hashlib.sha256()
        with part.open("xb") as out:
            for offset in range(0, size, self.chunk_size):
                n = min(self.chunk_size, size - offset)
                r = self.request("flash_get", None, offset, n)
                if len(r) != n + 9 or r[:1] != b"R" or struct.unpack("<Q", r[1:9])[0] != offset:
                    raise TransferError("Invalid flash read reply")
                out.write(r[9:])
                sha.update(r[9:])
                if offset % (1024 * 1024) == 0:
                    print("Flash backed up:", offset, "/", size, flush=True)
        part.rename(target)
        print("Flash backup SHA256:", sha.hexdigest(), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--key", type=Path, default=os.environ.get("TFN_ADMIN_KEY"))
    sub = ap.add_subparsers(dest="op", required=True)
    cmd = sub.add_parser("command")
    cmd.add_argument("words", nargs="+")
    ls = sub.add_parser("ls")
    ls.add_argument("path")
    get = sub.add_parser("get")
    get.add_argument("path")
    get.add_argument("--out", type=Path, required=True)
    install = sub.add_parser("install")
    install.add_argument("package", type=Path)
    install.add_argument("--backup", type=Path, required=True)
    install.add_argument(
        "--sign-key",
        type=Path,
        default=os.environ.get("TFN_APP_SIGNING_KEY"),
        help="Sign the package locally without changing its source folder",
    )
    update = sub.add_parser("update")
    update.add_argument("image", type=Path)
    backup = sub.add_parser("flash-backup")
    backup.add_argument("out", type=Path)
    a = ap.parse_args()
    if not a.key:
        ap.error("--key or TFN_ADMIN_KEY is required")
    import serial

    port = serial.Serial()
    port.port = a.port
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 5
    port.dtr = port.rts = False
    try:
        with port:
            port.reset_input_buffer()
            client = Maintenance(port, a.key)
            if a.op == "command":
                print("\n".join(client.command(" ".join(a.words))))
            elif a.op == "ls":
                for e in client.entries(a.path):
                    print("DIR" if e.directory else "FILE", e.size, e.name)
            elif a.op == "get":
                print(client.download(a.path, a.out))
            elif a.op == "install":
                client.install(a.package, a.backup, a.sign_key)
            elif a.op == "update":
                client.update(a.image)
            elif a.op == "flash-backup":
                client.flash_backup(a.out)
    except (OSError, TransferError) as e:
        ap.exit(1, str(e) + "\n")


if __name__ == "__main__":
    main()
