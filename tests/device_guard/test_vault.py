"""Production vault C code with real AES-GCM and interrupted filesystem writes."""

import argparse, ctypes as C, os, contextlib, _ctypes
from pathlib import Path
import subprocess, tempfile
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ap = argparse.ArgumentParser()
ap.add_argument("--cc", nargs="+", default=["clang"])
a = ap.parse_args()
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="tfn-vault-") as tmp, contextlib.ExitStack() as cleanup:
    root = Path(tmp)
    (root / "data").mkdir()
    library = root / ("vault.dll" if os.name == "nt" else "vault.so")
    subprocess.run(
        a.cc
        + [
            "-shared",
            "-std=c11",
            "-O1",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DESP_PLATFORM",
            "-DDEVICE_VAULT_HOST",
            "-I" + str(here / "inc"),
            str(here / "vault_adapter.c"),
            "-o",
            str(library),
        ],
        check=True,
    )
    lib = C.CDLL(str(library))
    size = C.c_size_t
    ptr = C.c_void_p
    cleanup.callback(_ctypes.FreeLibrary if os.name == "nt" else _ctypes.dlclose, lib._handle)
    Crypto = C.CFUNCTYPE(C.c_int, C.c_int, ptr, ptr, size, ptr, size, ptr, size, C.POINTER(size))
    Random = C.CFUNCTYPE(None, ptr, size)

    @Crypto
    def crypto(enc, nonce, aad, an, data, n, out, cap, used):
        key = (C.c_ubyte * 32)()
        lib.test_key(key)
        try:
            aes = AESGCM(bytes(key))
            args = (C.string_at(nonce, 12), C.string_at(data, n), C.string_at(aad, an))
            result = aes.encrypt(*args) if enc else aes.decrypt(*args)
            if len(result) > cap:
                return -1
            C.memmove(out, result, len(result))
            used[0] = len(result)
            return 0
        except Exception:
            return -1

    @Random
    def random(out, n):
        C.memmove(out, os.urandom(n), n)

    lib.test_config.argtypes = [C.c_char_p, Crypto, Random]
    lib.test_config(str(root).encode(), crypto, random)
    lib.device_guard_vault_init.restype = C.c_bool
    lib.device_guard_accounts_read.argtypes = [ptr, size]
    lib.device_guard_accounts_write.argtypes = [C.c_char_p, size]

    def read():
        out = C.create_string_buffer(12289)
        n = lib.device_guard_accounts_read(out, 12288)
        return n, bytes(out[: max(0, n)])

    def write(value):
        return lib.device_guard_accounts_write(value, len(value))

    old = b"AILIM-ACCT 1\nc.rt=old-test-token\n"
    new = b"AILIM-ACCT 1\nc.rt=new-test-token\n"
    legacy = root / "data/ai_limits.key"
    legacy.write_bytes(old)
    assert lib.device_guard_vault_init() and not legacy.exists()
    assert read()[1] == old
    assert write(old) == 1
    paths = [root / "data/ai_accounts.enc", root / "data/ai_accounts.tmp"]
    baseline = [p.read_bytes() for p in paths]
    assert all(old not in b for b in baseline)
    # Simulate a reset after every possible short write and after a close error.
    for cut in range(len(new) + 48 + 1):
        for p, b in zip(paths, baseline):
            p.write_bytes(b)
        lib.test_reset(0)
        assert lib.device_guard_vault_init()
        lib.test_fault(cut, 0, 0)
        result = write(new)
        lib.test_reset(0)
        assert lib.device_guard_vault_init()
        assert read()[1] == (new if cut >= len(new) + 48 else old)
        assert result == (1 if cut >= len(new) + 48 else -1)
    for p, b in zip(paths, baseline):
        p.write_bytes(b)
    lib.test_reset(0)
    assert lib.device_guard_vault_init()
    lib.test_fault(-1, 1, 0)
    assert write(new) == -1
    lib.test_reset(0)
    assert lib.device_guard_vault_init() and read()[1] == new
    # Corrupt each authenticated header, nonce, ciphertext and tag byte.
    assert write(old) == 1
    assert write(new) == 1
    current = [p.read_bytes() for p in paths]
    latest = max(range(2), key=lambda i: int.from_bytes(current[i][24:32], "little"))
    for offset in range(len(current[latest])):
        for p, b in zip(paths, current):
            p.write_bytes(b)
        bad = bytearray(current[latest])
        bad[offset] ^= 1
        paths[latest].write_bytes(bad)
        lib.test_reset(0)
        assert lib.device_guard_vault_init() and read()[1] == old
    for p in paths:
        p.write_bytes(b"broken")
    lib.test_reset(0)
    assert not lib.device_guard_vault_init()
    assert read()[0] == -1
    # Switch from the software-staging key to a hardware-derived key. Cut each
    # migration write, restart, and require the newest accounts to survive.
    for cut in range(len(new) + 48 + 1):
        for p, b in zip(paths, current):
            p.write_bytes(b)
        lib.test_reset(2)
        lib.test_hardware(1)
        lib.test_fault(cut, 0, 0)
        lib.device_guard_vault_init()
        lib.test_reset(0)
        assert lib.device_guard_vault_init() and read()[1] == new
        imported = (C.c_ubyte * 32)()
        lib.test_key(imported)
        assert bytes(imported) == b"\xc7" * 32
        assert all(
            AESGCM(bytes(imported)).decrypt(p.read_bytes()[8:20], p.read_bytes()[32:], p.read_bytes()[:32])
            == new
            for p in paths
        )
    lib.test_reset(0)
    lib.test_hardware(-1)
    assert not lib.device_guard_vault_init()
    lib.test_hardware(1)
    lib.test_mount(0)
    lib.test_reset(0)
    assert not lib.device_guard_vault_init()
    lib.test_mount(1)
    assert lib.device_guard_vault_init() and read()[1] == new
    lib.test_hardware(0)
    for p, b in zip(paths, current):
        p.write_bytes(b)
    lib.test_reset(1)
    assert not lib.device_guard_vault_init()  # never replace a lost key
    print(
        "VAULT OK: real AES-GCM, interrupted writes and hardware-key migration, close failure, tampering and missing keys"
    )
