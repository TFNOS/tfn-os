"""Verified enrollment transaction. Virtual mode never opens a USB port."""

from pathlib import Path
import contextlib
import json
import os
import time
from device_image import FLASH_BYTES, sha
from device_prepare import KEY_FILES, key_hashes, outside_repository, verify_prepared
from device_keys import write_new

SECURITY_FLAGS = {
    "SPI_BOOT_CRYPT_CNT": 7,
    "DIS_DOWNLOAD_ICACHE": 1,
    "DIS_DOWNLOAD_DCACHE": 1,
    "DIS_DOWNLOAD_MANUAL_ENCRYPT": 1,
    "DIS_USB_JTAG": 1,
    "DIS_PAD_JTAG": 1,
    "DIS_DIRECT_BOOT": 1,
    "DIS_USB_OTG_DOWNLOAD_MODE": 1,
    "SECURE_BOOT_KEY_REVOKE2": 1,
    "SECURE_BOOT_EN": 1,
}
LOCK_FIELDS = ("SPI_BOOT_CRYPT_CNT", "RD_DIS", "DIS_ICACHE", "SECURE_BOOT_EN")
PURPOSES = ("XTS_AES_128_KEY", "HMAC_UP", "SECURE_BOOT_DIGEST0", "SECURE_BOOT_DIGEST1")
KEY_INPUTS = ("flash_encryption.bin", "nvs_hmac.bin", "firmware_sign-digest.bin", "recovery_sign-digest.bin")


def durable_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".pending")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def numeric(value):
    if isinstance(value, (int, bool)):
        return int(value)
    return int(str(value), 0)


def bits(state, name):
    # Some IDF fields have a human meaning such as "Enable"/"Disable".
    # Compare their raw bits, particularly the three-bit encryption counter.
    return numeric(state[name].get("raw_value", state[name]["value"]))


class Journal:
    def __init__(self, path, binding, resume=False):
        self.path = Path(path)
        if self.path.exists():
            if not resume:
                raise ValueError("Enrollment receipt exists; inspect it and use --resume")
            self.value = json.loads(self.path.read_text(encoding="utf-8"))
            if self.value.get("binding") != binding:
                raise ValueError("Resume identity, keys or image differs from the recorded transaction")
        else:
            if resume:
                raise ValueError("No enrollment receipt to resume")
            self.value = {"format": 1, "binding": binding, "done": [], "intent": None, "events": []}
            durable_json(self.path, self.value)

    def save(self, event):
        self.value["events"].append({"event": event, "time": int(time.time())})
        durable_json(self.path, self.value)

    def step(self, name, action, verify, fault=None):
        if name in self.value["done"]:
            if not verify():
                raise ValueError("Recorded enrollment state no longer matches: " + name)
            return
        pending = self.value.get("intent") == name
        self.value["intent"] = name
        self.save("intent:" + name)
        if fault:
            fault("before:" + name)
        if not (pending and verify()):
            action()
        if fault:
            fault("after:" + name)
        if not verify():
            raise ValueError("Enrollment verification failed: " + name)
        self.value["done"].append(name)
        self.value["intent"] = None
        self.save("verified:" + name)


class EfuseBackend:
    """One reserved connection; each fuse query creates a fresh IDF view."""

    def __init__(
        self, directory, *, port=None, virtual=False, source=None, mac=None, reserved_connection=None
    ):
        import esptool
        import espefuse

        self.directory = Path(directory)
        self.virtual = virtual
        self.port_name = port
        self.esptool = esptool
        self.espefuse = espefuse
        if virtual:
            if port or source is None or not mac:
                raise ValueError(
                    "Virtual mode requires a file-backed flash and synthetic identity, never a port"
                )
            self.flash_path = self.directory / "virtual-flash.bin"
            if not self.flash_path.exists():
                self.flash_path.write_bytes(source)
            self.mac = mac
            self.esp = espefuse.get_esp(
                chip="esp32s3", virt=True, virt_efuse_file=str(self.directory / "virtual-efuses.bin")
            )
        else:
            if not port:
                raise ValueError("Physical enrollment requires an explicit USB port")
            self.esp = reserved_connection or esptool.detect_chip(
                port=port, baud=115200, connect_mode="no-reset"
            )
            if self.esp.CHIP_NAME != "ESP32-S3":
                raise ValueError("Enrollment supports ESP32-S3 lcd349 only")
            info = self.esp.get_security_info(cache=False)
            if info["parsed_flags"].get("SECURE_DOWNLOAD_ENABLE"):
                raise ValueError("Secure ROM is already active; use authenticated owner inspection/recovery")
            self.mac = bytes(self.esp.read_mac()).hex()
            if reserved_connection is None:
                self.esp = self.esp.run_stub()
                self.esp.change_baud(921600)

    def read_flash(self):
        if self.virtual:
            return self.flash_path.read_bytes()
        return self.esp.read_flash(0, FLASH_BYTES)

    def write_flash(self, data):
        if len(data) != FLASH_BYTES:
            raise ValueError("Enrollment flash image must contain exactly 16 MiB")
        if self.virtual:
            with self.flash_path.open("wb") as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
        else:
            self.esptool.write_flash(
                self.esp, [(0, data)], flash_size="keep", flash_mode="keep", flash_freq="keep", force=True
            )

    def fuse(self, *arguments):
        prefix = ["--chip", "esp32s3", "--before", "no-reset", "--after", "no-reset", "--do-not-confirm"]
        if self.virtual:
            prefix += ["--virt", "--path-efuse-file", str(self.directory / "virtual-efuses.bin")]
        else:
            prefix += ["--port", self.port_name]
        original = self.espefuse.get_esp

        def forbidden(*unused, **kwargs):
            raise RuntimeError("Fuse tool tried to replace the reserved connection")

        self.espefuse.get_esp = forbidden
        log = self.directory / "efuse-operations.log"
        try:
            with log.open("a", encoding="utf-8") as output, contextlib.redirect_stdout(output):
                self.espefuse.cli(
                    args=prefix + [str(value) for value in arguments],
                    esp=self.esp,
                    obj={"esp": self.esp},
                    standalone_mode=False,
                )
        finally:
            self.espefuse.get_esp = original

    def state(self):
        path = self.directory / ("state-" + str(time.time_ns()) + ".json")
        self.fuse("summary", "--format", "json", "--file", path)
        return json.loads(path.read_text(encoding="utf-8"))

    def secure_info(self):
        if not self.virtual:
            return self.esp.get_security_info(cache=False)
        state = self.state()
        return {
            "flash_crypt_cnt": bits(state, "SPI_BOOT_CRYPT_CNT"),
            "parsed_flags": {
                "SECURE_BOOT_EN": bool(state["SECURE_BOOT_EN"]["value"]),
                "SECURE_DOWNLOAD_ENABLE": bool(state["ENABLE_SECURITY_DOWNLOAD"]["value"]),
            },
        }

    def close(self, success=False):
        if not self.virtual:
            try:
                if success:
                    self.esp.hard_reset()
            finally:
                self.esp._port.close()


def key_bytes(state, index):
    entry = state["BLOCK_KEY" + str(index)]
    if not entry["readable"]:
        return None
    return bytes.fromhex(entry["value"])


def verify_key_backup(keys, backup):
    keys, backup = outside_repository(keys), outside_repository(backup)
    if keys == backup:
        raise ValueError("Recovery key backup must be a separate directory")
    hashes = key_hashes(keys)
    for name in KEY_FILES:
        if sha((backup / name).read_bytes()) != hashes[name]:
            raise ValueError("Recovery key backup differs: " + name)
    return hashes


def activate(backend, prepared, keys, key_backup, backup, journal_path, *, confirm, resume=False, fault=None):
    """Apply only a verified image; journal every irreversible step first.

    Fresh secret keys stay readable until their exact bytes are verified and
    durably recorded. Their read protection comes before RD_DIS is locked.
    Partial or unexpected key contents stop; they are never blindly re-burned.
    """
    prepared, keys, backup = Path(prepared), Path(keys), Path(backup)
    manifest = json.loads((prepared / "manifest.json").read_text(encoding="utf-8"))
    identity = json.loads((backup / "identity.json").read_text(encoding="utf-8"))
    source = (backup / "flash.bin").read_bytes()
    verify_prepared(prepared, keys, source)
    image = (prepared / "recovery-full-encrypted.bin").read_bytes()
    hashes = verify_key_backup(keys, key_backup)
    if (
        manifest.get("format") != 1
        or manifest.get("kind") != "lcd349"
        or not manifest.get("web_password_verified")
        or sha(source) != manifest["source_sha256"]
        or sha(image) != manifest["image_sha256"]
        or len(source) != FLASH_BYTES
        or len(image) != FLASH_BYTES
        or hashes != manifest["key_hashes"]
    ):
        raise ValueError("Prepared image, source backup or key verification failed")
    if (
        backend.mac != identity.get("mac")
        or confirm != backend.mac
        or identity.get("kind") != "lcd349"
        or identity.get("source_sha256") != sha(source)
    ):
        raise ValueError("Connected device is not the confirmed, backed-up lcd349")
    binding = {
        "mac": backend.mac,
        "source_sha256": sha(source),
        "image_sha256": sha(image),
        "key_hashes": hashes,
        "virtual": backend.virtual,
    }
    journal = Journal(journal_path, binding, resume)

    def field(name):
        return backend.state()[name]

    initial = backend.state()
    if not journal.value.get("preflight_verified"):
        if bits(initial, "SPI_BOOT_CRYPT_CNT") or initial["SECURE_BOOT_EN"]["value"]:
            raise ValueError("Board already has permanent security; enrollment is refused")
        if not initial["RD_DIS"]["writeable"]:
            raise ValueError("Unsupported read-protection lock state")
        for index in range(6):
            purpose = initial["KEY_PURPOSE_" + str(index)]["value"]
            if (
                purpose not in ("USER", 0)
                or key_bytes(initial, index) != b"\0" * 32
                or not initial["BLOCK_KEY" + str(index)]["writeable"]
                or not initial["KEY_PURPOSE_" + str(index)]["writeable"]
            ):
                raise ValueError("A key slot is already used; enrollment is refused")
        current = backend.read_flash()
        if current != source or backend.read_flash() != current:
            raise ValueError("Board data changed after backup; make a new held-ROM backup and prepare again")
        journal.value["preflight_verified"] = True
        journal.save("preflight-verified")
    if not backend.virtual:
        binding_path = keys / "enrolled-device.json"
        identity_binding = {"mac": backend.mac, "key_hashes": hashes}
        if binding_path.exists():
            if json.loads(binding_path.read_text(encoding="utf-8")) != identity_binding:
                raise ValueError("These hardware keys are already bound to another device")
        else:
            write_new(binding_path, (json.dumps(identity_binding, indent=2) + "\n").encode("utf-8"))

    def write_image():
        state = backend.state()
        if bits(state, "SPI_BOOT_CRYPT_CNT") or state["SECURE_BOOT_EN"]["value"]:
            raise ValueError("Cannot repeat a raw image write after hardware security started")
        if any(
            key_bytes(state, index) != b"\0" * 32
            or state["KEY_PURPOSE_" + str(index)]["value"] not in ("USER", 0)
            for index in range(6)
        ):
            raise ValueError("Key state changed before the verified image write")
        backend.write_flash(image)

    journal.step("flash-readback", write_image, lambda: backend.read_flash() == image, fault)

    for index, (purpose, name) in enumerate(zip(PURPOSES, KEY_INPUTS)):
        path = (keys if index < 2 else prepared) / name
        value = path.read_bytes()
        if len(value) != 32:
            raise ValueError("Invalid prepared key or signing digest")
        expected = value[::-1] if index == 0 else value
        step = "key-" + str(index)

        def verified(index=index, purpose=purpose, expected=expected, step=step):
            state = backend.state()
            if state["KEY_PURPOSE_" + str(index)]["value"] != purpose:
                return False
            raw = key_bytes(state, index)
            return (raw == expected or (raw is None and step in journal.value["done"])) and not state[
                "BLOCK_KEY" + str(index)
            ]["writeable"]

        def burn(index=index, purpose=purpose, path=path, expected=expected):
            state = backend.state()
            old = key_bytes(state, index)
            current_purpose = state["KEY_PURPOSE_" + str(index)]["value"]
            if old == expected and current_purpose == purpose:
                return
            if old != b"\0" * 32 or current_purpose not in ("USER", 0):
                raise ValueError("Unexpected or partial key burn; no retry: slot " + str(index))
            backend.fuse("burn-key", "--no-read-protect", "BLOCK_KEY" + str(index), path, purpose)

        journal.step(step, burn, verified, fault)

    def protect_keys():
        state = backend.state()
        readable = [
            "BLOCK_KEY" + str(index) for index in (0, 1) if state["BLOCK_KEY" + str(index)]["readable"]
        ]
        if readable:
            backend.fuse("read-protect-efuse", *readable)

    journal.step(
        "secret-read-protection",
        protect_keys,
        lambda: all(not backend.state()["BLOCK_KEY" + str(index)]["readable"] for index in (0, 1)),
        fault,
    )

    def enable_flags():
        state = backend.state()
        arguments = []
        for name, desired in SECURITY_FLAGS.items():
            current = bits(state, name)
            if current & ~desired:
                raise ValueError("Unexpected security flag: " + name)
            if current != desired:
                arguments.extend((name, desired))
        if arguments:
            backend.fuse("burn-efuse", *arguments)

    def flags_verified():
        state = backend.state()
        return all(bits(state, name) == value for name, value in SECURITY_FLAGS.items())

    journal.step("security-flags", enable_flags, flags_verified, fault)
    journal.step(
        "security-write-locks",
        lambda: backend.fuse("write-protect-efuse", *LOCK_FIELDS),
        lambda: all(not backend.state()[name]["writeable"] for name in LOCK_FIELDS),
        fault,
    )
    state = backend.state()
    if state["DIS_USB_SERIAL_JTAG"]["value"] or state["DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE"]["value"]:
        raise ValueError("Native USB recovery was disabled; refusing the final ROM step")

    def secure_verified():
        info = backend.secure_info()
        return (
            info["flash_crypt_cnt"] == 7
            and info["parsed_flags"]["SECURE_BOOT_EN"]
            and info["parsed_flags"]["SECURE_DOWNLOAD_ENABLE"]
        )

    journal.step(
        "secure-rom-last",
        lambda: backend.fuse("burn-efuse", "ENABLE_SECURITY_DOWNLOAD", 1),
        secure_verified,
        fault,
    )
    journal.value["complete"] = True
    journal.save("complete")
    return journal.value
