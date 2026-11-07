"""Real prepared-image and virtual enrollment integration; never opens USB.

Supply a device_build output, synthetic owner keys, their separate backup and
an AI LIMITS SDK package. The test uses only a generated stock flash fixture.
"""

from pathlib import Path
from types import SimpleNamespace
import argparse
import contextlib
import io
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/tools"))
from device_enroll import EfuseBackend, activate, durable_json
from device_image import sha, STOCK_OFFSETS
from device_keys import private_directory, write_new
from device_prepare import prepare, verify_prepared
from device_storage import write_fat
from test_image import stock_flash


def run(build, keys, key_backup, package, output):
    from esp_idf_nvs_partition_gen import nvs_partition_gen

    output = output.resolve()
    private_directory(output)
    backup = output / "backup"
    private_directory(backup)
    csv = output / "synthetic.csv"
    csv.write_text(
        "key,type,encoding,value\nwebauth,namespace,,\n"
        "salt,data,hex2bin," + bytes(range(16)).hex() + "\n"
        "hash,data,hex2bin," + bytes(range(32)).hex() + "\n"
        "rounds,data,u32,2000\nfixture,namespace,,\n"
        "name,data,string,Synthetic test owner\n"
        "wifi,data,string,Synthetic test network\n",
        encoding="ascii",
    )
    args = SimpleNamespace(
        input=str(csv), output=str(output / "nvs.bin"), outdir=str(output), version=2, size="0x6000"
    )
    with contextlib.redirect_stdout(io.StringIO()):
        nvs_partition_gen.generate(args)
    flash = stock_flash()
    offset, size = STOCK_OFFSETS["nvs"]
    flash[offset : offset + size] = (output / "nvs.bin").read_bytes()
    files = {
        "settings.txt": b"Synthetic settings\n",
        "notes/Long Name.txt": b"Unrelated file must remain byte-identical",
        "notes/שלום.txt": bytes(range(256)),
        "ai_limits.key": b"AILIM-ACCT 1\nc.rt=synthetic-not-a-credential\n",
        "ai_limits.kew": b"interrupted synthetic write",
    }
    offset, size = STOCK_OFFSETS["storage"]
    flash[offset : offset + size] = write_fat(files, {"notes", "empty"}, size)
    write_new(backup / "flash.bin", flash)
    identity = {
        "format": 1,
        "kind": "lcd349",
        "mac": "020000000001",
        "source_sha256": sha(flash),
        "second_read_sha256": sha(flash),
    }
    durable_json(backup / "identity.json", identity)
    prepared = output / "prepared"
    result = prepare(flash, build, keys, prepared, ai_package=package, trust_installed_apps=True)
    assert verify_prepared(prepared, keys, flash) == result
    image = prepared / "recovery-full-encrypted.bin"
    good = image.read_bytes()
    damaged = bytearray(good)
    damaged[0x720100] ^= 1
    image.write_bytes(damaged)
    try:
        verify_prepared(prepared, keys, flash)
    except ValueError:
        pass
    else:
        raise AssertionError("Damaged ciphertext was accepted")
    finally:
        image.write_bytes(good)
    transaction = output / "activation"
    private_directory(transaction)
    backend = EfuseBackend(transaction, virtual=True, source=flash, mac=identity["mac"])
    try:
        receipt = activate(
            backend, prepared, keys, key_backup, backup, transaction / "receipt.json", confirm=identity["mac"]
        )
        assert receipt["complete"]
    finally:
        backend.close(success=False)
    print(
        "PASS: real signed build, NVS/FAT/vault preservation, ciphertext rejection, full virtual enrollment"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "keys", "key-backup", "ai-package", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if sys.flags.optimize:
        parser.error("Assertions must be enabled")
    run(args.build, args.keys, args.key_backup, args.ai_package, args.out)
