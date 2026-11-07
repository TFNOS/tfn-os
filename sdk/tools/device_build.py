"""Build a per-owner protected image from an immutable source capture; no USB."""

from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from device_image import espsecure, idf_path, sha
from device_keys import private_directory, write_new
from device_prepare import outside_repository, validate_build


def capture(source, destination):
    source = Path(source).resolve()
    git = ["git", "-C", str(source)]
    result = subprocess.run(
        git + ["ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        capture_output=True,
        check=True,
    )
    names = sorted(set(name for name in result.stdout.decode("utf-8").split("\0") if name))
    excluded = ("sdkconfig", "sdkconfig.cores3se", "sdkconfig.amoled18", "sdkconfig.watcher")
    manifest = {}
    for name in names:
        path = source / name
        if name in excluded or name.startswith((".git/", ".secrets/")):
            continue
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(source):
            raise ValueError("Source capture contains an unsupported file")
        data = path.read_bytes()
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        manifest[name] = sha(data)
    for name, digest in manifest.items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError("Source changed during capture; preserve this attempt and run a new build")
    if (
        not {
            "CMakeLists.txt",
            "main/apps.json",
            "sdk/device-protection.defaults",
            "partitions_protected349.csv",
        }
        <= manifest.keys()
    ):
        raise ValueError("Source is not a complete protection-capable checkout")
    content = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode("utf-8")).hexdigest()
    commit = subprocess.check_output(git + ["rev-parse", "HEAD"], text=True).strip()
    return content, commit


def build(source, keys, output, *, jobs=6, version=None):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa

    keys, output = outside_repository(keys), outside_repository(output)
    key = serialization.load_pem_private_key((keys / "firmware_sign.pem").read_bytes(), None)
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 3072:
        raise ValueError("Protected firmware requires the enrolled RSA-3072 signing identity")
    public = key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    if public != (keys / "firmware_sign.pub.pem").read_bytes():
        raise ValueError("Firmware signing public/private key mismatch")
    private_directory(output)
    work = output / "source"
    work.mkdir()
    content, commit = capture(source, work)
    version = version or "DEV-OWNER-" + content[:12]
    if (
        not version.isascii()
        or not 1 <= len(version) <= 31
        or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._" for c in version)
    ):
        raise ValueError("Use a 1-31 character build version with letters, digits, dash, dot or underscore")
    public_header = output / "owner-public-keys.h"
    shutil.copyfile(keys / "public_keys.h", public_header)
    defaults = output / "owner.defaults"
    defaults.write_text(
        "CONFIG_SECURE_BOOT_SIGNING_KEY="
        + json.dumps((keys / "firmware_sign.pem").as_posix())
        + "\nCONFIG_APP_REPRODUCIBLE_BUILD=y\n",
        encoding="utf-8",
    )
    target = output / "build"
    environment = dict(os.environ)
    environment["CMAKE_NINJA_FORCE_RESPONSE_FILE"] = "1"
    environment["CMAKE_BUILD_PARALLEL_LEVEL"] = str(jobs)
    configs = [work / "sdkconfig.defaults", work / "sdk/device-protection.defaults", defaults]
    command = [
        sys.executable,
        str(idf_path() / "tools/idf.py"),
        "-C",
        str(work),
        "-B",
        str(target),
        "-D",
        "IDF_TARGET=esp32s3",
        "-D",
        "SDKCONFIG=" + str(output / "sdkconfig"),
        "-D",
        "SDKCONFIG_DEFAULTS=" + ";".join(map(str, configs)),
        "-D",
        "PROJECT_VER=" + version,
        "-D",
        "ARCADE_SOURCE_SHA=" + content,
        "-D",
        "ARCADE_DEVICE_PUBLIC_HEADER=" + str(public_header),
        "build",
    ]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copyfile(public_header, target / "owner-public-keys.h")
    validate_build(target, keys, require_record=False)
    espsecure(
        "verify-signature",
        "--version",
        "2",
        "--keyfile",
        keys / "firmware_sign.pub.pem",
        target / "tfn_os.bin",
    )
    meta = {
        "format": 1,
        "kind": "lcd349",
        "protected": True,
        "version": version,
        "source_commit": commit,
        "source_sha256": content,
        "image_sha256": sha((target / "tfn_os.bin").read_bytes()),
        "elf_sha256": sha((target / "tfn_os.elf").read_bytes()),
        "owner_public_sha256": sha(public_header.read_bytes()),
        "firmware_public_sha256": sha(public),
        "partition_md5": hashlib.md5(
            (target / "partition_table/partition-table.bin").read_bytes()
        ).hexdigest(),
    }
    abi = json.loads((work / "main/tfn_abi.json").read_text(encoding="utf-8"))
    meta["abi_rev"] = abi["rev"]
    write_new(target / "owner-build.json", (json.dumps(meta, indent=2) + "\n").encode("utf-8"))
    validate_build(target, keys)
    return target, meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--keys", type=Path, required=True, help="Private owner build identity outside Git")
    parser.add_argument("--out", type=Path, required=True, help="New private build directory outside Git")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--version")
    args = parser.parse_args()
    try:
        target, meta = build(args.source, args.keys, args.out, jobs=args.jobs, version=args.version)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(
            1,
            "Protected build stopped: "
            + type(error).__name__
            + ". Preserve the private output and inspect build.log.\n",
        )
    print("Protected signed build ready:", target)
    print("Version:", meta["version"], "SHA-256:", meta["image_sha256"])
    print("No device was connected or modified.")


if __name__ == "__main__":
    main()
