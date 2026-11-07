# Device protection without a startup PIN

Use the [setup and integration checklist](device-protection-setup.md) during enrollment.

**Web manager compatibility:** Protected mode was **NOT tested with the web
manager** using a real protected board. It can have compatibility problems with
the web manager. For boards managed by the web manager, protected mode is
**NOT COMPATIBLE until a live run with a real protected board passes**.

## What another user gets

The default firmware behaves as before. `CONFIG_TFN_DEVICE_PROTECTION` is
OFF, and no owner key or hardware security is required. Installing a normal
update does not enable protection or burn security eFuses.

Protection is an advanced, per-device USB enrollment process for the 16 MiB
Waveshare lcd349. The tools below implement backup, migration, verification
and enrollment. Other boards and layouts are refused. Do not distribute one person's protected binary as
a general release: it trusts that person's public keys.

Users who do not want encryption leave it disabled on an unprovisioned board.
Permanent hardware encryption cannot later be disabled through a menu or by
turning the software option off. Keep the protected profile on enrolled boards.

The protected lcd349 profile uses encrypted PSRAM for cryptographic allocations.
It also uses eight static Wi-Fi transmit buffers and sends general allocations
larger than 512 bytes to PSRAM first. This leaves internal RAM for SD DMA,
task stacks and USB maintenance. Ordinary builds keep their existing settings.

## Create an owner identity on the computer

Install `cryptography` and `pyserial` into the Python environment used by the
SDK. Choose a new private directory OUTSIDE the repository:

```
python sdk/tools/device_keys.py --out ../private-device-keys/board-1
```

This command creates files only. It does not connect to the board, enable
protection or burn eFuses. Existing directories are refused so a second run
cannot replace an enrolled identity. The folder is restricted to the current
user (and SYSTEM on Windows); private keys are unencrypted PEM files. Protect
the computer account and keep an encrypted, offline backup of this folder.

| File | Purpose |
| --- | --- |
| `usb_admin.pem` | Prove ownership for USB file access and maintenance. |
| `app_sign.pem` | Sign owner-built app packages. Upstream app signatures remain accepted. |
| `firmware_sign.pem` | Sign compatible protected firmware updates. |
| `recovery_sign.pem` | Independent recovery firmware signing key. Store a separate backup. |
| `flash_encryption.bin` | Per-device flash encryption key material. Never reuse on another board. |
| `nvs_hmac.bin` | Per-device HMAC key for NVS and the AI account vault. Never reuse on another board. |
| `public_keys.h` | Only the public USB/app verification keys; supply to the protected build. |
| `*.pub.pem`, `fingerprints.json` | Verify backups and identify the generated trust material. |

The board stores public USB/app verification keys in signed firmware. The owner
private USB key stays on the computer. Flash/HMAC keys are different keys and
become read-protected on the board. The owner USB key does not decrypt storage.

## Enroll a board

Key creation is not enrollment. A protected firmware must embed the matching
`public_keys.h`; generating another key later does not make the board trust it.
There is no unauthenticated USB pairing or reset command that adds a new owner.

Use the ESP-IDF 6.1 Python environment, with its normal compiler environment
loaded. `device_build.py` captures the source, loads the opt-in hardware profile,
embeds the owner's public keys and signs the build. It checks the native
configuration, layout, image, ELF and owner fingerprints before it writes an
`owner-build.json` record. Its private output must be outside the repository.

```
python sdk/tools/device_build.py --keys ../private-device-keys/board-1 --out ../owner-builds/board-1/first
python sdk/tools/device_provision.py inspect --port PORT --rom --out ../board-identity.json
```

The ROM inspection restarts a fresh board but writes no flash or eFuses. Use
the reported MAC as `MAC` below. On an already protected board, use
`inspect --port PORT --owner-key ../private-device-keys/board-1/usb_admin.pem`.
Never run fresh enrollment again on it.

Before any permanent eFuse operation, the provisioning process must:

1. Verify board identity, security state and available key slots. Back up and
   verify the full flash, SD files, source, configuration and all keys.
2. Require an existing web password. If none exists, stop and ask the owner to
   choose one in SETUP before enrollment. Never replace a password to test it.
3. Build signed firmware and a bootloader for the protected partition layout.
   Migrate the existing NVS without losing its values and prepare native HMAC
   NVS encryption. Prepare signed, pre-encrypted recovery images for the exact
   board key and flash addresses; independently verify all round trips.
4. Verify the complete encrypted flash write before burning security eFuses.
   Provision keys before locking read-protection controls. Enable secure boot,
   release flash encryption and debug restrictions in the documented IDF order.
   Enable secure ROM download mode last, keeping the required USB serial path.
5. Verify normal boot, saved settings, app signatures, owner access, encrypted
   vault migration, readout rejection and the signed update/recovery paths.

Copy the entire key directory to separate secure storage first. The tool checks
all six private/hardware keys against that independent copy before any write.
Keep both copies on encrypted disks or in an encrypted backup. Record the public
fingerprints separately. Back up the SD card with the existing SD download tool
if needed; enrollment never writes the card.

For existing AI accounts, build an AI LIMITS package with the protected SDK
configuration (ABI rev 17), using `mk_app.py "AI LIMITS" --kinds lcd349
--firmware-build BUILD --sign-key KEYDIR/app_sign.pem`. Supply its package directory as `PACKAGE`.
The generated protected build's `config/sdkconfig.h` must be used by the SDK
build when it checks conditional APIs. Existing installed apps are signed only
after explicit `--trust-installed-apps`; review their source before granting
that trust. The tool verifies every manifest content hash first.

```
python sdk/tools/device_provision.py enroll --port PORT --confirm-device MAC --build ../owner-builds/board-1/first/build --keys ../private-device-keys/board-1 --key-backup ../offline-key-backup/board-1 --ai-package PACKAGE --trust-installed-apps --out ../enrollment/board-1 --apply
```

This command reserves one USB connection. It verifies two full flash reads,
prepares and independently decrypts every encrypted region, checks both RSA
signatures, preserves logical NVS and unrelated FAT files, and verifies the
complete encrypted flash readback before the first fuse burn. The legacy AI
account pair becomes two authenticated vault generations. It checks each
key's actual bytes before read-protecting it. Secret read protection precedes
the locks. Secure ROM download is the last phase. A durable receipt records
each intent and verified result. No reset occurs between migration and fuses.

For an offline review, use `backup`, then `prepare`, then `activate --virtual`
with the same backup, prepared image and keys. Virtual mode refuses a USB port.
For physical activation use `--apply --port PORT`. `enroll` is preferred because
it prevents the original firmware from changing settings after the backup.

After an interruption, preserve the receipt, complete outputs and keys. Do not
erase the board or regenerate keys. `activate --resume` checks recorded states
and refuses partial key contents. Virtual faults cover each phase boundary;
this is not a claim that every physical reset state can resume. An unexpected
ROM restriction stops the tool. If only the final receipt was lost after
secure ROM download became active, use `finish --port PORT --keys KEYDIR
--receipt RECEIPT --confirm-device MAC`: it checks the final ROM flags, boots
the verified image, authenticates the expected owner/MAC, and records completion
without another fuse write. Other incomplete hardware states need an expert
review of the receipt before any further operation.

The protected table is at `0xE000`; NVS moves to `0x712000` to make room for the
signed bootloader. Enabling the software option alone does not migrate this
layout, encrypt an existing device or provide physical protection. Do not flash
it over an existing board using the normal app-only flash command. Do not copy
provisioning commands or an encrypted image from a different board.

The account-vault interface is restricted to AI LIMITS. Other credential apps
need their own reviewed storage integration. The AI account migration expects
the existing AI LIMITS account format. An old
AI app that reads the plaintext login file must be updated to the vault APIs
(ABI 1 rev 17) before protection is enabled. AI LIMITS is included in
`apps/ai_limits/`, disabled by default, with its focused host tests and SDK
package metadata. Ordinary app builds retain their existing account storage.

## Daily use and updates

Normal boot, Wi-Fi and ordinary apps need no PIN. The device hotspot and phone
web file browser use the existing web password. SD PCAP files stay ordinary,
unencrypted files. Local sensitive controls require SETUP > ADMIN ACCESS using
that web password; no computer is required for those controls.

USB maintenance uses the private owner key:

```
python sdk/tools/device_admin.py --port PORT --key ../private-device-keys/board-1/usb_admin.pem command admin
```

The host signs a short-lived challenge. The private key is not sent to the
device. Another computer can use a secure copy of the same owner key. Key
rotation requires an authorized, compatible firmware update that embeds the
replacement public key; it is not automatic pairing with any USB computer.

Protected firmware updates must match the protected layout and carry a valid
signature from an enrolled firmware key. Generic stock firmware is not a safe
update for a protected board. Existing unsigned apps must be signed; developer
mode cannot bypass verification.

The following describes implemented manager support, not verified compatibility.
The restriction above applies to boards managed by the web manager.
The manager implementation uses the existing web password.
In its private `config.json`, add a `protected_devices` map keyed by the
uppercase twelve-digit MAC. Each entry has these local fields:

| Field | Value |
| --- | --- |
| `keys` | Private directory with the enrolled firmware/app signing keys, their public files, `public_keys.h`, and `enrolled-device.json`. |
| `source` | Complete protection-capable Git checkout to capture and build. |
| `output` | Private parent directory for owner build attempts, outside Git. |
| `python` | Optional absolute path to the IDF Python interpreter. |
| `trust_store` | Explicit `true` to trust and owner-sign packages from this manager's local store. |
| `build` | Optional verified build directory under `output` to select an existing artifact. |

Enrollment creates `enrolled-device.json`; retain it with the final key backup.
The manager checks its MAC and signer hashes. It does not need the flash/HMAC
secrets or recovery private key. Keep those offline. Run the manager in the IDF
Python environment for owner artifact verification. Ordinary manager operation
retains its existing Python requirements.

DEVICES > FIRMWARE shows BUILD OWNER UPDATE for a configured protected board.
This creates a signed protected build from an immutable capture. INSTALL sends
only the verified owner image. Every send rechecks the security configuration,
layout, owner identity, hashes and RSA signature. A stock library update is
refused, including the ordinary update queue. A board that reports protection
without a profile is refused too. The API returns status, never local key paths
or key bytes. The manager owner controls the source checkout and signing keys.
Use it only on the trusted LAN; its existing HTTP administration and Basic
authentication do not provide transport encryption.

For app installs, explicit `trust_store` lets the manager sign a verified
package manifest in memory with that board's app key. The shared store files
stay unchanged. An older AI LIMITS package without the vault ABI is refused.

For a USB update, use `device_admin.py --port PORT --key KEYDIR/usb_admin.pem
update BUILD/tfn_os.bin`. Use a verified protected owner build, never a
stock binary. This writes the inactive app slot and selects it for boot; it
does not replace NVS, files, the bootloader or the partition table.

Loss of every copy of the owner key prevents owner USB authentication. Loss of
all enrolled firmware signing keys prevents future compatible signed updates.
Keep verified backups before provisioning. The SD card and ordinary unlocked
screens remain accessible; this feature does not lock the entire device.

If the PC is lost, restore the same key directory and compatible source/build
records from the offline backup on another protected computer. Authenticate
with the same USB key. If the primary firmware signer is lost, a recovery
signer can sign a compatible build; it cannot decrypt storage. Flash/HMAC
backups can decrypt the owner's saved recovery image offline, so keep them as
carefully as credentials. Recovery images contain the original settings and
become stale: do not overwrite live NVS/FAT with an old full image to repair an
app update. Prefer the signed inactive-slot USB update. The original stock
dump is evidence and a data backup, not a bootable restore after enrollment.

The security boundary is physical flash/USB extraction of stored secrets.
Signed firmware can use the chip's hardware keys without revealing them. No
startup PIN is needed. An unlocked app, a trusted signed malicious app, a known
web password, the trusted manager, or an advanced hardware fault attack remain
outside this boundary. SD captures intentionally stay plaintext.
