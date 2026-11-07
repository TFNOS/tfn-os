"""USB-only owner enrollment for fresh lcd349 boards; protection is opt-in.

Use inspect --owner-key for an already secured board. Never attempt a fresh
enrollment or a stock firmware restore on an enrolled device.
"""

from pathlib import Path
import argparse
import json
import re
import sys
import time
from device_image import FLASH_BYTES, require_web_password, sha, stock_layout
from device_prepare import prepare, outside_repository, validate_build
from device_keys import private_directory, write_new
from device_enroll import EfuseBackend, activate, durable_json, verify_key_backup


def normalized_mac(value):
    value = value.replace(":", "").replace("-", "").lower()
    if not re.fullmatch("[0-9a-f]{12}", value):
        raise ValueError("Use the exact twelve hexadecimal digits of the owned device MAC")
    return value


def owner_inspect(port_name, key):
    """Authenticate without changing a setting, then lock the USB session."""
    import serial
    from device_admin import Maintenance

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = port_name, 115200, 0.1
    port.write_timeout, port.dtr, port.rts = 5, False, False
    with port:
        port.reset_input_buffer()
        client = Maintenance(port, key)
        try:
            lines = client.command("admin", 0.4)
            status = next((line for line in lines if " PROTECTED " in line), None)
            if status is None:
                raise ValueError("Peer did not report protected status")
            result = {"kind": "lcd349", "mac": client.authorization.mac, "already_enrolled": True}
            for field in ("secure_boot", "flash_encryption", "nvs_encryption"):
                match = re.search(r"\b" + field + r"=(0|1)\b", status)
                if not match:
                    raise ValueError("Peer security status was incomplete")
                result[field] = match[1] == "1"
            return result
        finally:
            port.write(b"admin lock\r\n")
            time.sleep(0.1)


def rom_inspect(port_name):
    """Read fresh-board identity without changing flash, fuses or settings."""
    import esptool

    esp = esptool.detect_chip(port=port_name, baud=115200, connect_mode="default-reset")
    try:
        info = esp.get_security_info(cache=False)
        if esp.CHIP_NAME != "ESP32-S3":
            raise ValueError("Only ESP32-S3 is supported")
        if info["parsed_flags"].get("SECURE_DOWNLOAD_ENABLE"):
            raise ValueError("Already secured: inspect using --owner-key, without --rom")
        mac = bytes(esp.read_mac()).hex()
        return {
            "mac": mac,
            "already_enrolled": bool(info["flash_crypt_cnt"] or info["parsed_flags"].get("SECURE_BOOT_EN")),
            "security": info,
        }
    finally:
        try:
            esp.hard_reset()
        finally:
            esp._port.close()


def finish_enrollment(port_name, keys, receipt, confirm):
    """Recover only a lost final receipt, never a missing/partial fuse phase."""
    import esptool
    from device_prepare import key_hashes
    from device_enroll import PURPOSES

    value = json.loads(receipt.read_text(encoding="utf-8"))
    done = (
        ["flash-readback"]
        + ["key-" + str(i) for i in range(len(PURPOSES))]
        + ["secret-read-protection", "security-flags", "security-write-locks"]
    )
    if (
        value.get("complete")
        or value.get("done") != done
        or value.get("intent") != "secure-rom-last"
        or value["binding"]["virtual"]
        or value["binding"]["mac"] != normalized_mac(confirm)
        or value["binding"]["key_hashes"] != key_hashes(keys)
    ):
        raise ValueError("Receipt is not a physical transaction interrupted at the final ROM step")
    esp = esptool.detect_chip(port=port_name, baud=115200, connect_mode="default-reset")
    try:
        info = esp.get_security_info(cache=False)
        if (
            esp.CHIP_NAME != "ESP32-S3"
            or info["flash_crypt_cnt"] != 7
            or not info["parsed_flags"].get("SECURE_BOOT_EN")
            or not info["parsed_flags"].get("SECURE_DOWNLOAD_ENABLE")
        ):
            raise ValueError("Final security state is not complete; no fuse writes attempted")
        esp.hard_reset()
    finally:
        esp._port.close()
    time.sleep(3)
    state = owner_inspect(port_name, keys / "usb_admin.pem")
    if state["mac"] != normalized_mac(confirm) or not all(
        state[name] for name in ("secure_boot", "flash_encryption", "nvs_encryption")
    ):
        raise ValueError("Running firmware did not authenticate the expected protected device")
    value["done"].append("secure-rom-last")
    value["intent"] = None
    value["complete"] = True
    value["events"].append({"event": "owner-verified-final-receipt", "time": int(time.time())})
    durable_json(receipt, value)


def backup_board(port_name, output, *, confirm=None, hold=True):
    """Double-read before any write; leave the chip in ROM for preparation."""
    import esptool

    output = outside_repository(output)
    private_directory(output)
    esp = None
    success = False
    try:
        esp = esptool.detect_chip(port=port_name, baud=115200, connect_mode="default-reset")
        info = esp.get_security_info(cache=False)
        if (
            esp.CHIP_NAME != "ESP32-S3"
            or info["flash_crypt_cnt"] != 0
            or info["parsed_flags"].get("SECURE_BOOT_EN")
            or info["parsed_flags"].get("SECURE_DOWNLOAD_ENABLE")
            or any(info["key_purposes"])
        ):
            raise ValueError("Not a fresh unprotected ESP32-S3; use owner inspection/recovery instead")
        mac = bytes(esp.read_mac()).hex()
        if confirm and mac != normalized_mac(confirm):
            raise ValueError("Connected device differs from the confirmed owner device")
        esp = esp.run_stub()
        esp.change_baud(921600)
        capacity = (esp.flash_id(cache=False) >> 16) & 0xFF
        if 1 << capacity != FLASH_BYTES:
            raise ValueError("Only the 16 MiB lcd349 profile is supported")
        source = esp.read_flash(0, FLASH_BYTES)
        second = esp.read_flash(0, FLASH_BYTES)
        if source != second:
            raise ValueError("Full flash backup differs on its independent second read")
        table = stock_layout(source)
        if b"@ARCADE-BOARD:lcd349" not in source[table["ota_0"]["offset"] : table["otadata"]["offset"]]:
            raise ValueError("No existing lcd349 firmware tag; board kind is not verified")
        part = table["nvs"]
        require_web_password(source[part["offset"] : part["offset"] + part["size"]])
        write_new(output / "flash.bin", source)
        if (output / "flash.bin").read_bytes() != source:
            raise ValueError("Saved flash backup readback differs")
        identity = {
            "format": 1,
            "kind": "lcd349",
            "mac": mac,
            "source_sha256": sha(source),
            "second_read_sha256": sha(second),
            "flash_bytes": len(source),
            "held_in_rom": hold,
            "security_before": info,
        }
        durable_json(output / "identity.json", identity)
        success = True
        if hold:
            reserved = esp
            esp = None
            return identity, source, reserved
        return identity, source, None
    finally:
        if esp:
            try:
                if not hold:
                    esp.hard_reset()
            finally:
                esp._port.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("inspect", help="Read security status; never enrolls")
    inspect.add_argument("--port", required=True)
    inspection = inspect.add_mutually_exclusive_group(required=True)
    inspection.add_argument("--owner-key", type=Path, help="Existing owner USB key")
    inspection.add_argument(
        "--rom", action="store_true", help="Fresh board only; reads ROM identity and restarts"
    )
    inspect.add_argument("--out", type=Path)
    finish = commands.add_parser("finish", help="Verify a completed final ROM step after a lost receipt")
    finish.add_argument("--port", required=True)
    finish.add_argument("--keys", type=Path, required=True)
    finish.add_argument("--receipt", type=Path, required=True)
    finish.add_argument("--confirm-device", required=True)
    backup = commands.add_parser("backup", help="Fresh-board double-read backup; holds ROM until a reset")
    backup.add_argument("--port", required=True)
    backup.add_argument("--out", type=Path, required=True)
    backup.add_argument("--confirm-device", required=True)
    offline = commands.add_parser("prepare", help="Offline verified migration; never opens USB")
    offline.add_argument("--backup", type=Path, required=True)
    offline.add_argument("--build", type=Path, required=True)
    offline.add_argument("--keys", type=Path, required=True)
    offline.add_argument("--out", type=Path, required=True)
    offline.add_argument("--ai-package", type=Path)
    offline.add_argument("--trust-installed-apps", action="store_true")
    activation = commands.add_parser(
        "activate", help="Apply a verified migration over physical USB, or virtually"
    )
    activation.add_argument("--port")
    activation.add_argument("--backup", type=Path, required=True)
    activation.add_argument("--prepared", type=Path, required=True)
    activation.add_argument("--keys", type=Path, required=True)
    activation.add_argument("--key-backup", type=Path, required=True)
    activation.add_argument("--out", type=Path, required=True)
    activation.add_argument("--confirm-device", required=True)
    activation.add_argument("--resume", action="store_true")
    mode = activation.add_mutually_exclusive_group(required=True)
    mode.add_argument("--virtual", action="store_true")
    mode.add_argument("--apply", action="store_true")
    enrollment = commands.add_parser(
        "enroll", help="Backup, prepare and activate on one reserved USB connection"
    )
    enrollment.add_argument("--port", required=True)
    enrollment.add_argument("--build", type=Path, required=True)
    enrollment.add_argument("--keys", type=Path, required=True)
    enrollment.add_argument("--key-backup", type=Path, required=True)
    enrollment.add_argument("--out", type=Path, required=True)
    enrollment.add_argument("--confirm-device", required=True)
    enrollment.add_argument("--apply", action="store_true", required=True)
    enrollment.add_argument("--ai-package", type=Path)
    enrollment.add_argument("--trust-installed-apps", action="store_true")
    args = parser.parse_args()
    backend = None
    reserved = None
    success = False
    try:
        if sys.flags.optimize:
            raise ValueError("Run enrollment with the normal Python interpreter, without -O")
        if args.command == "inspect":
            result = rom_inspect(args.port) if args.rom else owner_inspect(args.port, args.owner_key)
            if args.out:
                write_new(outside_repository(args.out), (json.dumps(result, indent=2) + "\n").encode("utf-8"))
            print("MAC:", result["mac"], "Already enrolled:", result["already_enrolled"])
        elif args.command == "finish":
            finish_enrollment(args.port, args.keys, args.receipt, args.confirm_device)
            print(
                "Final receipt restored after ROM and authenticated running-firmware verification. No fuse writes."
            )
        elif args.command == "backup":
            _, _, reserved = backup_board(args.port, args.out, confirm=args.confirm_device)
            print("Double-read backup verified. Device remains in ROM; no flash or fuse writes occurred.")
        elif args.command == "prepare":
            source = (args.backup / "flash.bin").read_bytes()
            identity = json.loads((args.backup / "identity.json").read_text(encoding="utf-8"))
            if identity["source_sha256"] != sha(source) or identity["kind"] != "lcd349":
                raise ValueError("Source backup identity does not match")
            prepare(
                source,
                args.build,
                args.keys,
                args.out,
                ai_package=args.ai_package,
                trust_installed_apps=args.trust_installed_apps,
            )
            print("Signed/encrypted recovery image prepared and independently verified. No device access.")
        else:
            confirm = normalized_mac(args.confirm_device)
            verify_key_backup(args.keys, args.key_backup)
            output = outside_repository(args.out)
            if args.command == "enroll":
                validate_build(args.build, args.keys)
                private_directory(output)
                _, source, reserved = backup_board(args.port, output / "backup", confirm=confirm)
                prepare(
                    source,
                    args.build,
                    args.keys,
                    output / "prepared",
                    ai_package=args.ai_package,
                    trust_installed_apps=args.trust_installed_apps,
                )
                run = output / "activation"
                private_directory(run)
                backend = EfuseBackend(run, port=args.port, reserved_connection=reserved)
                reserved = None
                result = activate(
                    backend,
                    output / "prepared",
                    args.keys,
                    args.key_backup,
                    output / "backup",
                    run / "receipt.json",
                    confirm=confirm,
                )
            else:
                if args.virtual and args.port:
                    raise ValueError("Virtual mode must not specify a physical USB port")
                if args.apply and not args.port:
                    raise ValueError("Physical activation requires an explicit USB port")
                if not args.resume:
                    private_directory(output)
                elif not output.is_dir():
                    raise ValueError("No transaction directory to resume")
                source = (args.backup / "flash.bin").read_bytes()
                backend = EfuseBackend(
                    output, port=args.port, virtual=args.virtual, source=source, mac=confirm
                )
                result = activate(
                    backend,
                    args.prepared,
                    args.keys,
                    args.key_backup,
                    args.backup,
                    output / "receipt.json",
                    confirm=confirm,
                    resume=args.resume,
                )
            success = result.get("complete", False)
            print("Enrollment verified:", "virtual, no USB" if backend.virtual else "owned USB device")
            if not backend.virtual:
                print(
                    "Boot the device and use inspect with the owner key to verify the running protected firmware."
                )
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(
            1,
            "Enrollment stopped: "
            + str(error)
            + "\nPreserve the transaction directory and backups. Do not reset or blindly retry an incomplete fuse step.\n",
        )
    finally:
        if backend:
            backend.close(success=success)
        if reserved:
            reserved._port.close()


if __name__ == "__main__":
    main()
