"""Offline lcd349 migration primitives. This module never opens a board."""

from pathlib import Path
import hashlib
import hmac
import os
import struct
import subprocess
import sys

FLASH_BYTES = 0x1000000
VAULT_CONTEXT = b"TFN AI account vault key v1".ljust(32, b"\0")
STOCK_OFFSETS = {
    "nvs": (0x9000, 0x6000),
    "phy_init": (0xF000, 0x1000),
    "ota_0": (0x10000, 0x380000),
    "ota_1": (0x390000, 0x380000),
    "otadata": (0x710000, 0x2000),
    "storage": (0x720000, 0x8E0000),
}
PART_TYPES = {
    "nvs": (1, 2),
    "phy_init": (1, 1),
    "ota_0": (0, 0x10),
    "ota_1": (0, 0x11),
    "otadata": (1, 0),
    "storage": (1, 0x81),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def idf_path():
    path = Path(os.environ.get("IDF_PATH", ""))
    if not (path / "components/nvs_flash/nvs_partition_tool/nvs_parser.py").is_file():
        raise ValueError("Use the ESP-IDF Python environment and set IDF_PATH")
    return path.resolve()


def partitions(table):
    """Read an IDF table only after checking its MD5 and bounded regions."""
    result = {}
    checksum = False
    for offset in range(0, min(len(table), 0xC00), 32):
        entry = table[offset : offset + 32]
        if len(entry) != 32:
            raise ValueError("Truncated partition entry")
        if entry[:2] == b"\xeb\xeb":
            if hashlib.md5(table[:offset]).digest() != entry[16:32]:
                raise ValueError("Partition-table MD5 does not match")
            checksum = True
            break
        magic, kind, subtype, address, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if magic != 0x50AA or not size or address % 4096 or size % 4096 or address + size > FLASH_BYTES:
            raise ValueError("Invalid or unsupported partition table")
        name = label.rstrip(b"\0").decode("ascii")
        if not name or name in result:
            raise ValueError("Invalid partition name")
        if any(
            address < item["offset"] + item["size"] and item["offset"] < address + size
            for item in result.values()
        ):
            raise ValueError("Overlapping partitions")
        result[name] = {"type": kind, "subtype": subtype, "offset": address, "size": size, "flags": flags}
    if not checksum:
        raise ValueError("Partition table has no verified checksum")
    return result


def stock_layout(flash):
    if len(flash) != FLASH_BYTES:
        raise ValueError("Enrollment supports only the 16 MiB lcd349 stock slim layout")
    table = partitions(flash[0x8000:0x9000])
    if set(table) != set(STOCK_OFFSETS):
        raise ValueError("Unsupported stock partition names")
    for name, (address, size) in STOCK_OFFSETS.items():
        item = table[name]
        if (item["offset"], item["size"], item["flags"]) != (address, size, 0):
            raise ValueError("Unsupported stock layout: " + name)
        if (item["type"], item["subtype"]) != PART_TYPES[name]:
            raise ValueError("Unsupported partition type: " + name)
    return table


def nvs_values(raw):
    """Use IDF's parser; values remain in memory and are never printed."""
    sys.path.insert(0, str(idf_path() / "components/nvs_flash/nvs_partition_tool"))
    from nvs_parser import NVS_Partition

    part = NVS_Partition("nvs", bytearray(raw))
    pages = sorted(
        (page for page in part.pages if not page.is_empty and page.header["status"] in ("Active", "Full")),
        key=lambda page: page.header["page_index"],
    )
    entries = [entry for page in pages for entry in page.entries if entry.state == "Written"]
    namespaces = {entry.data["value"]: entry.key for entry in entries if entry.metadata["namespace"] == 0}
    values = {}
    chunks = {}
    for entry in entries:
        space = entry.metadata["namespace"]
        if not space:
            continue
        key = (namespaces[space], entry.key)
        kind = entry.metadata["type"]
        if kind in ("string", "blob", "blob_data"):
            value = b"".join(bytes(child.raw) for child in entry.children)[: entry.data["size"]]
            if kind == "blob_data":
                chunks[(key, entry.metadata["chunk_index"])] = value
            else:
                values[key] = value
        elif kind == "blob_index":
            values[key] = b"".join(
                chunks[(key, index)]
                for index in range(
                    entry.data["chunk_start"], entry.data["chunk_start"] + entry.data["chunk_count"]
                )
            )[: entry.data["size"]]
        else:
            values[key] = entry.data["value"]
    return values


def require_web_password(nvs):
    values = nvs_values(nvs)
    salt = values.get(("webauth", "salt"), b"")
    digest = values.get(("webauth", "hash"), b"")
    if (
        not isinstance(salt, (bytes, bytearray))
        or len(salt) != 16
        or not isinstance(digest, (bytes, bytearray))
        or len(digest) != 32
        or not isinstance(values.get(("webauth", "rounds")), int)
        or not 1 <= values[("webauth", "rounds")] <= 1000000
    ):
        raise ValueError("Set a web password in SETUP before enrollment; no board writes were made")
    if any(space in ("device_guard", "dgvault") for space, key in values):
        raise ValueError("Software-staged protection is not a fresh stock enrollment")
    return values


def nvs_xts_key(master):
    if len(master) != 32:
        raise ValueError("NVS HMAC key must contain 32 bytes")
    return hmac.digest(master, b"\x5a\x5a\xbe\xae" * 8, "sha256") + hmac.digest(
        master, b"\xa5\xa5\xde\xce" * 8, "sha256"
    )


def encrypt_nvs(source, master):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from esp_idf_nvs_partition_gen.nvs_partition_gen import decrypt_data

    if not source or len(source) % 4096:
        raise ValueError("NVS size must be a positive number of pages")
    key = nvs_xts_key(master)
    encrypted = bytearray(source)
    checked = bytearray(source)
    for page in range(len(source) // 4096):
        base = page * 4096
        for entry in range(126):
            offset = base + 64 + entry * 32
            plain = source[offset : offset + 32]
            bits = (source[base + 32 + entry // 4] >> ((entry % 4) * 2)) & 3
            if bits == 3 and plain == b"\xff" * 32:
                continue
            context = Cipher(algorithms.AES(key), modes.XTS(offset.to_bytes(16, "little"))).encryptor()
            encrypted[offset : offset + 32] = context.update(plain) + context.finalize()
            checked[offset : offset + 32] = decrypt_data(
                encrypted[offset : offset + 32], key, page, entry, 32
            )
    if bytes(checked) != source:
        raise ValueError("Independent IDF NVS decryption does not match the backup")
    return bytes(encrypted)


def vault_file(text, master, generation):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    if not text.startswith(b"AILIM-ACCT 1\n") or not 0 < len(text) <= 12288 or not 0 < generation < 2**64:
        raise ValueError("Invalid legacy AI account data")
    nonce = os.urandom(12)
    header = b"TFNACCT1" + nonce + struct.pack("<IQ", len(text), generation)
    key = hmac.digest(master, VAULT_CONTEXT, "sha256")
    aes = AESGCM(key)
    encrypted = aes.encrypt(nonce, text, header)
    if aes.decrypt(nonce, encrypted, header) != text:
        raise ValueError("AI vault verification failed")
    return header + encrypted


def espsecure(*arguments):
    result = subprocess.run(
        [sys.executable, "-m", "espsecure"] + [str(value) for value in arguments],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise ValueError("espsecure operation failed: " + str(arguments[0]))
    return result.stdout


def checked_encrypt(plain, offset, keyfile, directory, name):
    """Verify every region with official encrypt and decrypt commands."""
    directory = Path(directory)
    source = directory / (name + ".plain.bin")
    encrypted = directory / (name + ".cipher.bin")
    verified = directory / (name + ".verified.bin")
    for path in (source, encrypted, verified):
        if path.exists():
            raise FileExistsError("Preserve previous migration outputs: " + path.name)
    source.write_bytes(plain)
    espsecure(
        "encrypt-flash-data",
        "--aes-xts",
        "--keyfile",
        keyfile,
        "--address",
        hex(offset),
        "--output",
        encrypted,
        source,
    )
    espsecure(
        "decrypt-flash-data",
        "--aes-xts",
        "--keyfile",
        keyfile,
        "--address",
        hex(offset),
        "--output",
        verified,
        encrypted,
    )
    cipher = encrypted.read_bytes()
    if verified.read_bytes() != plain or len(cipher) != len(plain):
        raise ValueError("Flash region verification failed: " + name)
    return cipher
