"""Real offline NVS/FAT/AES migration checks; no USB and no owner credentials."""

from pathlib import Path
from types import SimpleNamespace
import contextlib
import hashlib
import io
import os
import struct
import sys
import tempfile
import unittest
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# The NVS checks use the NVS tool of ESP-IDF (the Python of ESP-IDF 6.1; sdk/docs/tools.md).
try:
    import esp_idf_nvs_partition_gen  # noqa: F401
except ImportError:
    sys.exit("test_image.py needs esp-idf-nvs-partition-gen (the Python of ESP-IDF 6.1)")

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/tools"))
import device_image as image
import device_storage as storage


def stock_flash(wrong_type=False):
    entries = bytearray()
    for name, (offset, size) in image.STOCK_OFFSETS.items():
        kind, subtype = image.PART_TYPES[name]
        if wrong_type and name == "storage":
            subtype = 0x82
        entries.extend(
            struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode().ljust(16, b"\0"), 0)
        )
    entries.extend(b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest())
    flash = bytearray(b"\xff" * image.FLASH_BYTES)
    flash[0x8000 : 0x8000 + len(entries)] = entries
    return flash


class ImageTests(unittest.TestCase):
    def test_layout_checksum_and_types(self):
        source = stock_flash()
        self.assertEqual(set(image.stock_layout(source)), set(image.STOCK_OFFSETS))
        source[0x8010] ^= 1
        with self.assertRaises(ValueError):
            image.stock_layout(source)
        with self.assertRaises(ValueError):
            image.stock_layout(stock_flash(wrong_type=True))
        with self.assertRaises(ValueError):
            image.stock_layout(b"\xff" * 4096)

    def test_fat_preserves_files_and_empty_directories(self):
        files = {
            "home.txt": b"unchanged settings",
            "notes/Long File Name.bin": bytes(range(256)) * 19,
            "notes/empty.txt": b"",
            "notes/שלום.txt": b"UTF-16 filename",
        }
        directories = {"notes", "exports"}
        volume = storage.write_fat(files, directories, 0x100000)
        self.assertEqual(storage.read_fat(volume), (files, directories))

    def test_unsafe_fat_names(self):
        for name in ("..", ".", "a/b", "a\\b", "C:", "tail.", "a\0b"):
            with self.assertRaises(ValueError):
                storage.safe_name(name)

    def test_native_fat_lowercase_flags(self):
        storage.write_fat({}, set(), 0x100000)
        import fatfsparse as parser
        from wl_fatfsgen import WLFATFS

        volume = WLFATFS(size=0x100000, sector_size=4096, sectors_per_cluster=1)
        volume.plain_fatfs.create_file("LOWER", "TXT")
        volume.plain_fatfs.write_content(["LOWER.TXT"], b"keep lowercase")
        volume.init_wl()
        raw = bytearray(parser.remove_wear_levelling_if_exists(bytes(volume.fatfs_binary_image)))
        boot = parser.BootSector()
        boot.parse_boot_sector(raw)
        entry = boot.boot_sector_state.root_directory_start
        while raw[entry : entry + 11] != b"LOWER   TXT":
            entry += 32
            self.assertLess(entry, len(raw))
        raw[entry + 12] = 0x18
        self.assertEqual(storage.read_fat(bytes(raw)), ({"lower.txt": b"keep lowercase"}, set()))
        self.assertEqual(
            storage.read_fat(storage.write_fat({"UPPER.TXT": b"case"}, set(), 0x100000)),
            ({"UPPER.TXT": b"case"}, set()),
        )

    def test_accounts_move_only_after_authenticated_encryption(self):
        master = os.urandom(32)
        text = b"AILIM-ACCT 1\nc.rt=synthetic-test-value\n"
        files = {"home.txt": b"keep", "ai_limits.key": text, "ai_limits.kew": b"interrupted write"}
        changes = storage.migrate_accounts(files, master)
        self.assertEqual(changes, {"ai_limits.key", "ai_limits.kew", "ai_accounts.enc", "ai_accounts.tmp"})
        self.assertEqual(files["home.txt"], b"keep")
        self.assertNotIn("ai_limits.key", files)
        key = image.hmac.digest(master, image.VAULT_CONTEXT, "sha256")
        for name, generation in [("ai_accounts.enc", 1), ("ai_accounts.tmp", 2)]:
            data = files[name]
            self.assertEqual(data[:8], b"TFNACCT1")
            self.assertEqual(struct.unpack("<IQ", data[20:32]), (len(text), generation))
            self.assertEqual(AESGCM(key).decrypt(data[8:20], data[32:], data[:32]), text)
        self.assertNotEqual(files["ai_accounts.enc"][8:20], files["ai_accounts.tmp"][8:20])
        with self.assertRaises(ValueError):
            storage.migrate_accounts(files, master)

    def test_invalid_accounts_are_kept(self):
        files = {"ai_limits.key": b"corrupt"}
        before = dict(files)
        with self.assertRaises(ValueError):
            storage.migrate_accounts(files, os.urandom(32))
        self.assertEqual(files, before)

    def test_native_nvs_and_password_precondition(self):
        from esp_idf_nvs_partition_gen import nvs_partition_gen

        with tempfile.TemporaryDirectory(prefix="nvs-image-test-") as temporary:
            directory = Path(temporary)
            source = directory / "test.csv"
            target = directory / "test.bin"
            source.write_text(
                "key,type,encoding,value\nwebauth,namespace,,\n"
                "salt,data,hex2bin," + bytes(range(16)).hex() + "\n"
                "hash,data,hex2bin," + bytes(range(32)).hex() + "\n"
                "rounds,data,u32,2000\n",
                encoding="ascii",
            )
            args = SimpleNamespace(
                input=str(source), output=str(target), outdir=str(directory), version=2, size="0x6000"
            )
            with contextlib.redirect_stdout(io.StringIO()):
                nvs_partition_gen.generate(args)
            original = target.read_bytes()
            values = image.require_web_password(original)
            self.assertEqual(values[("webauth", "salt")], bytes(range(16)))
            encrypted = image.encrypt_nvs(original, os.urandom(32))
            self.assertEqual(len(encrypted), len(original))
            self.assertNotEqual(encrypted, original)
            source.write_text(
                "key,type,encoding,value\nother,namespace,,\nnumber,data,u32,5\n", encoding="ascii"
            )
            args = SimpleNamespace(
                input=str(source), output=str(target), outdir=str(directory), version=2, size="0x6000"
            )
            with contextlib.redirect_stdout(io.StringIO()):
                nvs_partition_gen.generate(args)
            with self.assertRaises(ValueError):
                image.require_web_password(target.read_bytes())


if __name__ == "__main__":
    unittest.main()
