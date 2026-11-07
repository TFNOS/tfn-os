"""CLI inspection/final-receipt recovery: synthetic state, no USB access."""

from pathlib import Path
from types import SimpleNamespace
import json
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/tools"))
import device_provision as provision
import device_enroll as enroll

MAC = "020000000001"


class ProvisionTests(unittest.TestCase):
    def test_existing_firmware_status_is_parsed_and_session_locked(self):
        import serial
        import device_admin

        port = mock.MagicMock()
        port.__enter__.return_value = port
        client = mock.Mock()
        client.authorization.mac = MAC
        client.command.return_value = [
            "ADMIN AUTHORIZED PROTECTED secure_boot=1 flash_encryption=1 nvs_encryption=1"
        ]
        with mock.patch.object(serial, "Serial", return_value=port), mock.patch.object(
            device_admin, "Maintenance", return_value=client
        ), mock.patch.object(provision.time, "sleep"):
            result = provision.owner_inspect("synthetic", Path("synthetic-key.pem"))
        self.assertEqual(result["mac"], MAC)
        self.assertTrue(all(result[field] for field in ("secure_boot", "flash_encryption", "nvs_encryption")))
        port.write.assert_called_once_with(b"admin lock\r\n")

    def test_secure_rom_inspection_never_reads_forbidden_mac(self):
        import esptool

        esp = mock.Mock()
        esp.CHIP_NAME = "ESP32-S3"
        esp.get_security_info.return_value = {"parsed_flags": {"SECURE_DOWNLOAD_ENABLE": True}}
        with mock.patch.object(esptool, "detect_chip", return_value=esp):
            with self.assertRaises(ValueError):
                provision.rom_inspect("synthetic")
        esp.read_mac.assert_not_called()
        esp.hard_reset.assert_called_once()
        esp._port.close.assert_called_once()

    def test_final_receipt_requires_every_prior_durable_phase(self):
        import esptool

        with tempfile.TemporaryDirectory() as temporary:
            receipt = Path(temporary) / "receipt.json"
            receipt.write_text(
                json.dumps(
                    {"done": ["flash-readback"], "intent": "secure-rom-last", "binding": {"virtual": False}}
                )
            )
            with mock.patch.object(esptool, "detect_chip") as connect:
                with self.assertRaises(ValueError):
                    provision.finish_enrollment("synthetic", Path(temporary), receipt, MAC)
                connect.assert_not_called()

    def test_final_receipt_authenticates_booted_owner_before_completion(self):
        import esptool
        import device_prepare

        steps = [
            "flash-readback",
            "key-0",
            "key-1",
            "key-2",
            "key-3",
            "secret-read-protection",
            "security-flags",
            "security-write-locks",
        ]
        with tempfile.TemporaryDirectory() as temporary:
            receipt = Path(temporary) / "receipt.json"
            value = {
                "done": steps,
                "intent": "secure-rom-last",
                "events": [],
                "binding": {"virtual": False, "mac": MAC, "key_hashes": {"fixture": "hash"}},
            }
            receipt.write_text(json.dumps(value))
            esp = mock.Mock()
            esp.CHIP_NAME = "ESP32-S3"
            esp.get_security_info.return_value = {
                "flash_crypt_cnt": 7,
                "parsed_flags": {"SECURE_BOOT_EN": True, "SECURE_DOWNLOAD_ENABLE": True},
            }
            owner = {"mac": MAC, "secure_boot": True, "flash_encryption": True, "nvs_encryption": True}
            with mock.patch.object(
                device_prepare, "key_hashes", return_value={"fixture": "hash"}
            ), mock.patch.object(esptool, "detect_chip", return_value=esp), mock.patch.object(
                provision, "owner_inspect", return_value=owner
            ) as check, mock.patch.object(
                provision.time, "sleep"
            ):
                provision.finish_enrollment("synthetic", Path(temporary), receipt, MAC)
                check.assert_called_once()
            result = json.loads(receipt.read_text())
            self.assertTrue(result["complete"])
            self.assertEqual(result["done"][-1], "secure-rom-last")
            esp.write_flash.assert_not_called()


if __name__ == "__main__":
    unittest.main()
