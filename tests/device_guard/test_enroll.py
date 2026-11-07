"""The real eFuse CLI in virtual mode; power cuts cannot open a USB port."""

from pathlib import Path
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

import esptool

# The tools use the API of esptool 5 (the Python of ESP-IDF 6.1; sdk/docs/tools.md).
if int(esptool.__version__.split(".")[0]) < 5:
    sys.exit("test_enroll.py needs esptool 5 (the Python of ESP-IDF 6.1), not esptool " + esptool.__version__)

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/tools"))
import device_enroll as enroll
from device_keys import generate
from device_prepare import key_hashes
from device_image import FLASH_BYTES, sha

MAC = "020000000001"
STEPS = (
    "flash-readback",
    "key-0",
    "key-1",
    "key-2",
    "key-3",
    "secret-read-protection",
    "security-flags",
    "security-write-locks",
    "secure-rom-last",
)


class Cut(Exception):
    pass


class EnrollmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="virtual-enrollment-")
        cls.root = Path(cls.temporary.name)
        cls.keys = generate(cls.root / "keys")
        cls.key_backup = cls.root / "key-backup"
        shutil.copytree(cls.keys, cls.key_backup)
        cls.source = b"\xff" * FLASH_BYTES
        cls.image = b"\xa5" * FLASH_BYTES
        cls.backup = cls.root / "backup"
        cls.backup.mkdir()
        (cls.backup / "flash.bin").write_bytes(cls.source)
        (cls.backup / "identity.json").write_text(
            json.dumps({"mac": MAC, "kind": "lcd349", "source_sha256": sha(cls.source)})
        )
        cls.prepared = cls.root / "prepared"
        cls.prepared.mkdir()
        (cls.prepared / "recovery-full-encrypted.bin").write_bytes(cls.image)
        for name in ("firmware_sign-digest.bin", "recovery_sign-digest.bin"):
            (cls.prepared / name).write_bytes(os.urandom(32))
        cls.manifest = {
            "format": 1,
            "kind": "lcd349",
            "web_password_verified": True,
            "source_sha256": sha(cls.source),
            "image_sha256": sha(cls.image),
            "key_hashes": key_hashes(cls.keys),
        }
        (cls.prepared / "manifest.json").write_text(json.dumps(cls.manifest))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def backend(self, directory):
        return enroll.EfuseBackend(directory, virtual=True, source=self.source, mac=MAC)

    def run_enrollment(self, directory, *, resume=False, fault=None, confirm=MAC):
        backend = self.backend(directory)
        try:
            # This suite isolates the transaction against real virtual eFuses.
            # test_image.py and the full prepared-bundle test cover the actual
            # NVS/FAT/crypto validator; no executable image is claimed here.
            with mock.patch.object(enroll, "verify_prepared", return_value=self.manifest) as checked:
                result = enroll.activate(
                    backend,
                    self.prepared,
                    self.keys,
                    self.key_backup,
                    self.backup,
                    directory / "receipt.json",
                    confirm=confirm,
                    resume=resume,
                    fault=fault,
                )
                checked.assert_called_once()
                return result
        finally:
            backend.close()

    def test_virtual_complete(self):
        directory = self.root / "complete"
        directory.mkdir()
        result = self.run_enrollment(directory)
        self.assertTrue(result["complete"])
        self.assertEqual(result["done"], list(STEPS))
        state = self.backend(directory).state()
        self.assertFalse(state["BLOCK_KEY0"]["readable"])
        self.assertFalse(state["BLOCK_KEY1"]["readable"])
        self.assertTrue(state["BLOCK_KEY2"]["readable"])
        self.assertTrue(state["BLOCK_KEY3"]["readable"])
        self.assertFalse(state["DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE"]["value"])
        self.assertTrue(state["ENABLE_SECURITY_DOWNLOAD"]["value"])
        self.assertFalse((self.keys / "enrolled-device.json").exists())

    def test_confirmed_identity_required_before_flash(self):
        directory = self.root / "wrong-device"
        directory.mkdir()
        with self.assertRaises(ValueError):
            self.run_enrollment(directory, confirm="020000000002")
        self.assertEqual((directory / "virtual-flash.bin").read_bytes(), self.source)

    def test_readback_fault_cannot_burn_keys(self):
        directory = self.root / "readback-fault"
        directory.mkdir()
        backend = self.backend(directory)
        original_write = backend.write_flash

        def corrupt(data):
            original_write(data[:-1] + bytes([data[-1] ^ 1]))

        backend.write_flash = corrupt
        with mock.patch.object(enroll, "verify_prepared", return_value=self.manifest), self.assertRaises(
            ValueError
        ):
            enroll.activate(
                backend,
                self.prepared,
                self.keys,
                self.key_backup,
                self.backup,
                directory / "receipt.json",
                confirm=MAC,
            )
        state = backend.state()
        self.assertTrue(all(state["KEY_PURPOSE_" + str(index)]["value"] == "USER" for index in range(6)))
        self.assertEqual(enroll.bits(state, "SPI_BOOT_CRYPT_CNT"), 0)

    def test_used_slot_is_refused_even_when_retrying_preflight(self):
        directory = self.root / "used-slot"
        directory.mkdir()
        backend = self.backend(directory)
        backend.fuse("burn-key", "BLOCK_KEY5", self.keys / "nvs_hmac.bin", "HMAC_UP")
        for resume in (False, True):
            with mock.patch.object(enroll, "verify_prepared", return_value=self.manifest), self.assertRaises(
                ValueError
            ):
                enroll.activate(
                    backend,
                    self.prepared,
                    self.keys,
                    self.key_backup,
                    self.backup,
                    directory / "receipt.json",
                    confirm=MAC,
                    resume=resume,
                )
        self.assertEqual(backend.read_flash(), self.source)

    def test_source_change_is_not_overwritten(self):
        directory = self.root / "changed-source"
        directory.mkdir()
        backend = self.backend(directory)
        changed = b"\0" + self.source[1:]
        backend.write_flash(changed)
        with mock.patch.object(enroll, "verify_prepared", return_value=self.manifest), self.assertRaises(
            ValueError
        ):
            enroll.activate(
                backend,
                self.prepared,
                self.keys,
                self.key_backup,
                self.backup,
                directory / "receipt.json",
                confirm=MAC,
            )
        self.assertEqual(backend.read_flash(), changed)

    def test_changed_backup_key_refused_before_write(self):
        directory = self.root / "changed-key"
        directory.mkdir()
        recovery = self.root / "bad-key-backup"
        shutil.copytree(self.key_backup, recovery)
        (recovery / "nvs_hmac.bin").write_bytes(os.urandom(32))
        backend = self.backend(directory)
        with mock.patch.object(enroll, "verify_prepared", return_value=self.manifest), self.assertRaises(
            ValueError
        ):
            enroll.activate(
                backend,
                self.prepared,
                self.keys,
                recovery,
                self.backup,
                directory / "receipt.json",
                confirm=MAC,
            )
        self.assertEqual(backend.read_flash(), self.source)

    def test_each_before_after_cut_resumes_safely(self):
        for side in ("before", "after"):
            for step in STEPS:
                point = side + ":" + step
                with self.subTest(point=point):
                    directory = self.root / point.replace(":", "-")
                    directory.mkdir()

                    def fault(event):
                        if event == point:
                            raise Cut(point)

                    with self.assertRaises(Cut):
                        self.run_enrollment(directory, fault=fault)
                    result = self.run_enrollment(directory, resume=True)
                    self.assertTrue(result["complete"])
                    self.assertEqual(result["done"], list(STEPS))


if __name__ == "__main__":
    with contextlib.redirect_stdout(io.StringIO()):
        result = unittest.main(exit=False)
    raise SystemExit(not result.result.wasSuccessful())
