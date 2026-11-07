"""Prepare and verify a complete recovery image offline. No USB or fuse access."""

from pathlib import Path
import json
import re
from device_image import (
    FLASH_BYTES,
    PART_TYPES,
    checked_encrypt,
    encrypt_nvs,
    espsecure,
    partitions,
    require_web_password,
    sha,
    stock_layout,
    VAULT_CONTEXT,
)
from device_keys import private_directory, write_new
from device_storage import migrate_accounts, read_fat, sign_installed, write_fat

REQUIRED_CONFIG = (
    "TFN_DEVICE_PROTECTION",
    "TFN_TARGET_WS349",
    "SECURE_BOOT",
    "SECURE_FLASH_ENC_ENABLED",
    "SECURE_FLASH_ENCRYPTION_MODE_RELEASE",
    "SECURE_FLASH_ENCRYPTION_AES128",
    "NVS_ENCRYPTION",
    "NVS_SEC_KEY_PROTECT_USING_HMAC",
    "SECURE_ENABLE_SECURE_ROM_DL_MODE",
    "BOOTLOADER_APP_ROLLBACK_ENABLE",
)
KEY_FILES = (
    "usb_admin.pem",
    "app_sign.pem",
    "firmware_sign.pem",
    "recovery_sign.pem",
    "flash_encryption.bin",
    "nvs_hmac.bin",
)


def outside_repository(path):
    path = Path(path).expanduser().resolve()
    if path.is_relative_to(Path(__file__).resolve().parents[2]):
        raise ValueError("Keep private keys and migration outputs outside the repository")
    return path


def key_hashes(keys):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec, rsa

    keys = outside_repository(keys)
    result = {name: sha((keys / name).read_bytes()) for name in KEY_FILES}
    for name in ("flash_encryption.bin", "nvs_hmac.bin"):
        value = (keys / name).read_bytes()
        if len(value) != 32 or value in (b"\0" * 32, b"\xff" * 32):
            raise ValueError("Invalid per-device hardware key")
    if result["flash_encryption.bin"] == result["nvs_hmac.bin"]:
        raise ValueError("Flash and HMAC keys must be independent")
    for name in ("usb_admin", "app_sign", "firmware_sign", "recovery_sign"):
        key = serialization.load_pem_private_key((keys / (name + ".pem")).read_bytes(), None)
        if name in ("usb_admin", "app_sign"):
            valid = isinstance(key, ec.EllipticCurvePrivateKey) and isinstance(key.curve, ec.SECP256R1)
        else:
            valid = isinstance(key, rsa.RSAPrivateKey) and key.key_size == 3072
        if not valid:
            raise ValueError("Invalid owner key type: " + name)
        expected = key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        if (keys / (name + ".pub.pem")).read_bytes() != expected:
            raise ValueError("Owner public/private key mismatch")
    if (
        result["firmware_sign.pem"] == result["recovery_sign.pem"]
        or result["usb_admin.pem"] == result["app_sign.pem"]
    ):
        raise ValueError("Owner signing identities must be independent")
    return result


def validate_build(build, keys, *, require_record=True):
    build, keys = Path(build).resolve(), Path(keys).resolve()
    config = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    for option in REQUIRED_CONFIG:
        if not re.search(r"^#define CONFIG_" + option + r" 1$", config, re.M):
            raise ValueError("Protected build configuration missing: " + option)
    if not re.search(r"^#define CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID 1$", config, re.M):
        raise ValueError("Protected build must use HMAC key slot 1")
    if not re.search(r"^#define CONFIG_PARTITION_TABLE_OFFSET 0x[eE]000$", config, re.M):
        raise ValueError("Protected build partition offset must be 0xe000")
    if (build / "owner-public-keys.h").read_bytes() != (keys / "public_keys.h").read_bytes():
        raise ValueError("Build owner keys differ from the selected identity")
    from cryptography.hazmat.primitives import serialization

    for name in ("usb_admin", "app_sign"):
        key = serialization.load_pem_public_key((keys / (name + ".pub.pem")).read_bytes())
        raw = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        if raw not in (build / "tfn_os.bin").read_bytes():
            raise ValueError("Owner public key is absent from the compiled image")
    table = partitions((build / "partition_table/partition-table.bin").read_bytes())
    from device_image import STOCK_OFFSETS

    if set(table) != set(STOCK_OFFSETS):
        raise ValueError("Unsupported protected partition names")
    for name, (old_offset, size) in STOCK_OFFSETS.items():
        item = table[name]
        address = 0x712000 if name == "nvs" else old_offset
        flags = int(name in ("storage", "phy_init"))
        if (item["offset"], item["size"], item["flags"]) != (address, size, flags) or (
            item["type"],
            item["subtype"],
        ) != PART_TYPES[name]:
            raise ValueError("Unexpected protected partition: " + name)
    if require_record:
        meta = json.loads((build / "owner-build.json").read_text(encoding="utf-8"))
        if (
            meta.get("format") != 1
            or meta.get("kind") != "lcd349"
            or not meta.get("protected")
            or meta.get("abi_rev", 0) < 17
            or meta.get("image_sha256") != sha((build / "tfn_os.bin").read_bytes())
            or meta.get("elf_sha256") != sha((build / "tfn_os.elf").read_bytes())
            or meta.get("owner_public_sha256") != sha((keys / "public_keys.h").read_bytes())
        ):
            raise ValueError("Protected build record does not match its image, ABI or owner identity")
    return table


def prepare(source, build, keys, output, *, ai_package=None, trust_installed_apps=False):
    """All transformations finish and round-trip before any board writer runs."""
    source = bytes(source)
    original = stock_layout(source)
    nvs_part = original["nvs"]
    nvs = source[nvs_part["offset"] : nvs_part["offset"] + nvs_part["size"]]
    require_web_password(nvs)
    keys = outside_repository(keys)
    hashes = key_hashes(keys)
    build = Path(build).resolve()
    target_parts = validate_build(build, keys)
    output = outside_repository(output)
    private_directory(output)
    raw_part = original["storage"]
    files, directories = read_fat(source[raw_part["offset"] : raw_part["offset"] + raw_part["size"]])
    unchanged = dict(files)
    aliases = {name.casefold(): name for name in files}
    has_accounts = any(name in aliases for name in ("ai_limits.key", "ai_limits.kew"))
    if has_accounts and ai_package is None:
        raise ValueError("A protected AI LIMITS package is required to preserve existing account access")
    if (
        any(re.fullmatch(r"apps/[^/]+/manifest\.txt", name, re.I) for name in files)
        and not trust_installed_apps
    ):
        raise ValueError(
            "Review installed code, then supply --trust-installed-apps to enroll it as owner-trusted"
        )
    changes = set()
    if has_accounts and not any(name.casefold() == "apps/ai_limits/manifest.txt" for name in files):
        directory_aliases = {name.casefold(): name for name in directories}
        apps_root = directory_aliases.get("apps", "apps")
        app_root = apps_root + "/ai_limits"
        directories.update((apps_root, app_root))
        for name in ("app.elf", "icon.bin", "manifest.txt"):
            files[app_root + "/" + name] = (Path(ai_package) / name).read_bytes()
            changes.add(app_root + "/" + name)
    from cryptography.hazmat.primitives import serialization

    signing_key = serialization.load_pem_private_key((keys / "app_sign.pem").read_bytes(), None)
    changes |= sign_installed(files, signing_key, Path(ai_package) if ai_package else None)
    master = (keys / "nvs_hmac.bin").read_bytes()
    changes |= migrate_accounts(files, master)
    for name, value in unchanged.items():
        if name not in changes and files.get(name) != value:
            raise ValueError("Migration changed an unrelated user file")
    storage = write_fat(files, directories, raw_part["size"])
    encrypted_nvs = encrypt_nvs(nvs, master)
    signed = {}
    for name, filename in [("bootloader", "bootloader/bootloader.bin"), ("firmware", "tfn_os.bin")]:
        source_file = build / filename
        espsecure(
            "verify-signature", "--version", "2", "--keyfile", keys / "firmware_sign.pub.pem", source_file
        )
        target = output / (name + "-signed.bin")
        espsecure(
            "sign-data",
            "--version",
            "2",
            "--append-signatures",
            "--keyfile",
            keys / "recovery_sign.pem",
            "--output",
            target,
            source_file,
        )
        for owner in ("firmware_sign", "recovery_sign"):
            espsecure("verify-signature", "--version", "2", "--keyfile", keys / (owner + ".pub.pem"), target)
        signed[name] = target.read_bytes()
    for name in ("firmware_sign", "recovery_sign"):
        espsecure(
            "digest-sbv2-public-key",
            "--keyfile",
            keys / (name + ".pem"),
            "--output",
            output / (name + "-digest.bin"),
        )

    def pad(value, size):
        if len(value) > size:
            raise ValueError("Signed image exceeds its partition")
        return value + b"\xff" * (size - len(value))

    regions = {
        "bootloader": (0, pad(signed["bootloader"], 0xE000)),
        "partition-table": (0xE000, pad((build / "partition_table/partition-table.bin").read_bytes(), 4096)),
        "storage": (raw_part["offset"], storage),
    }
    for name in ("phy_init", "otadata", "ota_0", "ota_1"):
        part = target_parts[name]
        if name.startswith("ota_"):
            data = pad(signed["firmware"], part["size"])
        elif name == "otadata":
            data = pad((build / "ota_data_initial.bin").read_bytes(), part["size"])
        else:
            data = source[part["offset"] : part["offset"] + part["size"]]
        regions[name] = (part["offset"], data)
    complete = bytearray(b"\xff" * FLASH_BYTES)
    part = target_parts["nvs"]
    complete[part["offset"] : part["offset"] + part["size"]] = encrypted_nvs
    write_new(output / "nvs-encrypted.bin", encrypted_nvs)
    manifest = {
        "format": 1,
        "kind": "lcd349",
        "source_sha256": sha(source),
        "source_bytes": len(source),
        "key_hashes": hashes,
        "partitions": target_parts,
        "web_password_verified": True,
        "nvs_roundtrip": "identical, independently decrypted by IDF",
        "files": {name: sha(data) for name, data in files.items()},
        "changed_files": sorted(changes),
        "regions": {},
    }
    for name, (offset, plain) in regions.items():
        cipher = checked_encrypt(plain, offset, keys / "flash_encryption.bin", output, name)
        complete[offset : offset + len(cipher)] = cipher
        manifest["regions"][name] = {
            "offset": offset,
            "size": len(cipher),
            "plain_sha256": sha(plain),
            "cipher_sha256": sha(cipher),
        }
    manifest["image_sha256"] = sha(complete)
    write_new(output / "recovery-full-encrypted.bin", complete)
    write_new(output / "manifest.json", (json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
    return verify_prepared(output, keys, source)


def verify_prepared(output, keys, source):
    """Recheck actual recovery bytes and preserved user data before activation."""
    import hmac
    import struct
    import tempfile
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives import serialization, hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils

    output, keys = Path(output), Path(keys)
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    image = (output / "recovery-full-encrypted.bin").read_bytes()
    if (
        manifest["source_sha256"] != sha(source)
        or manifest["image_sha256"] != sha(image)
        or len(image) != FLASH_BYTES
        or manifest["key_hashes"] != key_hashes(keys)
    ):
        raise ValueError("Recovery image or identity changed since preparation")
    old = stock_layout(source)
    nvs_part = old["nvs"]
    nvs = source[nvs_part["offset"] : nvs_part["offset"] + nvs_part["size"]]
    require_web_password(nvs)
    master = (keys / "nvs_hmac.bin").read_bytes()
    target_nvs = image[0x712000:0x718000]
    if encrypt_nvs(nvs, master) != target_nvs:
        raise ValueError("Recovery image does not preserve the exact encrypted NVS backup")
    expected = {
        "bootloader": (0, 0xE000),
        "partition-table": (0xE000, 4096),
        **{name: (item["offset"], item["size"]) for name, item in old.items() if name != "nvs"},
    }
    if set(manifest["regions"]) != set(expected):
        raise ValueError("Recovery region set differs from the supported layout")
    plain_regions = {}
    with tempfile.TemporaryDirectory(prefix="verify-", dir=output) as temporary:
        temporary = Path(temporary)
        for name, (offset, size) in expected.items():
            info = manifest["regions"][name]
            cipher = image[offset : offset + size]
            if info["offset"] != offset or info["size"] != size or sha(cipher) != info["cipher_sha256"]:
                raise ValueError("Recovery region does not match its verified address: " + name)
            input_file, result_file = temporary / (name + ".bin"), temporary / (name + ".plain")
            input_file.write_bytes(cipher)
            espsecure(
                "decrypt-flash-data",
                "--aes-xts",
                "--keyfile",
                keys / "flash_encryption.bin",
                "--address",
                hex(offset),
                "--output",
                result_file,
                input_file,
            )
            plain = result_file.read_bytes()
            if sha(plain) != info["plain_sha256"] or plain != (output / (name + ".plain.bin")).read_bytes():
                raise ValueError("Recovery ciphertext does not decrypt to its verified region: " + name)
            plain_regions[name] = plain
        for signer in ("firmware_sign", "recovery_sign"):
            expected_digest = temporary / (signer + "-digest.bin")
            espsecure(
                "digest-sbv2-public-key", "--keyfile", keys / (signer + ".pem"), "--output", expected_digest
            )
            if expected_digest.read_bytes() != (output / (signer + "-digest.bin")).read_bytes():
                raise ValueError("Prepared secure-boot digest differs from its signing identity")
    target = partitions(plain_regions["partition-table"])
    if target != manifest["partitions"] or target["nvs"]["offset"] != 0x712000:
        raise ValueError("Encrypted partition table does not match the migration")
    if set(target) != set(old):
        raise ValueError("Prepared partition set changed")
    for name, item in target.items():
        previous = old[name]
        if (
            item["offset"] != (0x712000 if name == "nvs" else previous["offset"])
            or item["size"] != previous["size"]
            or (item["type"], item["subtype"]) != PART_TYPES[name]
            or item["flags"] != int(name in ("storage", "phy_init"))
        ):
            raise ValueError("Prepared partition is not the supported protected profile")
    for name, region in [("bootloader", "bootloader"), ("firmware", "ota_0")]:
        signed = (output / (name + "-signed.bin")).read_bytes()
        plain = plain_regions[region]
        if plain != signed + b"\xff" * (len(plain) - len(signed)):
            raise ValueError("Recovery image does not contain the verified signed image")
        for signer in ("firmware_sign", "recovery_sign"):
            espsecure(
                "verify-signature",
                "--version",
                "2",
                "--keyfile",
                keys / (signer + ".pub.pem"),
                output / (name + "-signed.bin"),
            )
    if plain_regions["ota_0"] != plain_regions["ota_1"]:
        raise ValueError("Both initial app slots must contain the verified image")
    original_part = old["storage"]
    before, before_dirs = read_fat(
        source[original_part["offset"] : original_part["offset"] + original_part["size"]]
    )
    after, after_dirs = read_fat(plain_regions["storage"])
    if {name: sha(data) for name, data in after.items()} != manifest[
        "files"
    ] or not before_dirs <= after_dirs:
        raise ValueError("Prepared file tree failed independent readback")
    for name, data in before.items():
        if name in after and after[name] == data:
            continue
        allowed = (
            name.casefold() in ("ai_limits.key", "ai_limits.kew")
            or re.fullmatch(r"apps/[^/]+/sig", name, re.I)
            or re.fullmatch(r"apps/ai_limits/(app\.elf|icon\.bin|manifest\.txt)", name, re.I)
        )
        if not allowed:
            raise ValueError("An unrelated user file changed during migration")
    owner = serialization.load_pem_public_key((keys / "app_sign.pub.pem").read_bytes())
    aliases = {name.casefold(): name for name in after}
    for name, raw in after.items():
        if not re.fullmatch(r"apps/[^/]+/manifest\.txt", name, re.I):
            continue
        root = name[:-13]
        signature = after[aliases[(root + "/sig").casefold()]]
        if len(signature) != 64:
            raise ValueError("Invalid enrolled app signature size")
        der = utils.encode_dss_signature(
            int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big")
        )
        owner.verify(der, raw, ec.ECDSA(hashes.SHA256()))
    before_aliases = {name.casefold(): name for name in before}
    account = next(
        (
            before[before_aliases[name]]
            for name in ("ai_limits.key", "ai_limits.kew")
            if name in before_aliases and before[before_aliases[name]].startswith(b"AILIM-ACCT 1\n")
        ),
        None,
    )
    if account is not None:
        aes = AESGCM(hmac.digest(master, VAULT_CONTEXT, "sha256"))
        for name, generation in [("ai_accounts.enc", 1), ("ai_accounts.tmp", 2)]:
            data = after[aliases[name]]
            if (
                data[:8] != b"TFNACCT1"
                or struct.unpack("<IQ", data[20:32]) != (len(account), generation)
                or aes.decrypt(data[8:20], data[32:], data[:32]) != account
            ):
                raise ValueError("Prepared vault did not preserve the original account data")
    return manifest
