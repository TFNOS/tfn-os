# Device protection setup and integration

Use this checklist with [device-protection.md](device-protection.md). It covers
backup, enrollment, recovery verification and integration into new apps.
Permanent enrollment requires the device owner to choose protection explicitly.

**Web manager compatibility:** Protected mode was **NOT tested with the web
manager** using a real protected board. It can have compatibility problems with
the web manager. For boards managed by the web manager, protected mode is
**NOT COMPATIBLE until a live run with a real protected board passes**.

## First determine the requested mode

| Owner choice | Unprovisioned board | Already protected board |
| --- | --- | --- |
| Ordinary firmware, no encryption | Keep `CONFIG_TFN_DEVICE_PROTECTION` OFF (the default), and leave native secure boot/flash/NVS encryption disabled. No owner key is needed. | Hardware encryption cannot be disabled. Keep the existing protected profile and explain this limit. Do not install an unprotected image. |
| Protect stored credentials | Follow the staged procedure below, with per-device keys and verified recovery. | Keep the enrolled keys, protected layout and signed update path. Do not generate replacements or repeat fuse provisioning. |

Protection is a build/provisioning choice, not a runtime SETTINGS toggle. Turning
the software option OFF does not undo eFuses or decrypt an enrolled device.
Ordinary users receive ordinary firmware by default. Do not change shared
sdkconfig defaults to enable protection for everyone.

Use `sdk/tools/device_build.py` and `device_provision.py` as documented in the
owner guide. Do not replace them with ad-hoc fuse commands. Key creation alone
is not enrollment. The supported profile is a stock-layout, 16 MiB lcd349 with
unused security key slots and an existing web password. Other layouts, boards
and existing security states stop before writes.

## 1. Inspect and preserve

1. Read the chip identity, hardware revision, flash size, security flags and
   key purposes without changing settings. Use the installed IDF/tools and
   official documentation for that exact chip. On an already protected board,
   use supported status commands; do not force forbidden readout operations.
2. Record the partition table, firmware version, app inventory, saved-setting
   status and existing web-password status. Do not print passwords, tokens,
   account identity claims, private key bytes or raw settings dumps.
3. Stop active apps at MENU. Make verified backups of all flash, internal files,
   SD files, source/configuration and existing keys. For an unprotected board,
   require two identical complete flash reads. Store sensitive backups outside
   Git with restricted access. Check file inventories and hashes.
4. Preserve every existing web password, saved Wi-Fi network, menu and app save.
   Never reset credentials to test authorization. If the owner has no web
   password, obtain their chosen password before protected-mode enrollment.

## 2. Create and back up new keys only for a new enrollment

Run on the owner's computer, outside the repository:

```
python sdk/tools/device_keys.py --out <new-private-per-device-directory>
```

The tool creates no board state and refuses an existing output directory.
Private PEM files are unencrypted, protected by file permissions. Arrange a
secure offline backup and independently verify it before provisioning.
Never reuse flash/HMAC key material on a different board. Never regenerate an
existing owner's keys to work around a failed login.

Use the matching public header through `TFN_DEVICE_PUBLIC_HEADER`. Only public
trust material may enter a build snapshot. Private signing keys and flash/HMAC
keys stay in owner-controlled storage outside Git. Record public fingerprints
and backup hashes in the private provisioning manifest.

## 3. Prepare software and recovery before permanent changes

1. First test a reversible software-only staging build on the existing layout:
   owner challenge authentication, local web-password unlock, private-route
   rejection, vault migration, signed apps and signed inactive-slot updates.
   Software staging alone does not protect physical flash readout.
2. Sign existing local apps before protected mode disables the unsigned-app
   developer bypass. Credential apps must use the vault APIs. AI LIMITS is
   included in this repository. Build its protected package with ABI 1 rev 17
   and `mk_app.py --firmware-build BUILD`; supply it to migration.
3. Build the production profile with secure boot v2, release flash encryption,
   native HMAC NVS encryption and the protected partition table at `0xE000`.
   NVS moves intact to `0x712000`; the app slots and internal FAT addresses stay
   the same. Preserve the entire logical settings image through migration.
4. Prepare signed bootloader/firmware and a signed, pre-encrypted recovery image
   for the exact enrolled keys and addresses. Independently verify signatures,
   all encryption round trips, range/size checks, data preservation and the key
   backup. Preserve an independent enrolled recovery signing key.
5. Run authorization, vault corruption/interruption, host transfer and update
   failure tests. Compile both the ordinary default configuration without an
   owner header and the intended protected configuration. Check actual board
   behavior without changing the owner's credentials.

Do not flash the production app alone onto the original partition layout. Do
not assume that toggling a Kconfig option safely migrates the board. Do not use
an encrypted image, fuse manifest or settings image from another device.

## 4. Permanent provisioning gate

Proceed only when explicitly authorized by the owner, the board identity is
verified, and all prior backup/staging/recovery checks passed. Use a reviewed,
resumable per-device plan with durable receipts and explicit phase assertions.

- Verify the full encrypted flash readback before any security fuse burn.
- Check unused key slots and enroll the intended flash, HMAC, primary firmware
  and recovery firmware trust material. Verify each phase before advancing.
- Protect secret keys before write-locking read-protection controls. Follow the
  chip's official secure-boot/flash-encryption ordering and debug restrictions.
- Keep the native USB serial/download path needed for signed/pre-encrypted
  recovery. Enable secure ROM download mode last.
- Request fresh security information after fuse changes. For esptool versions
  that cache `get_security_info`, use an uncached query rather than interpreting
  a stale response as failed provisioning.
- Validate the actual fuse-tool connection/reset behavior as well as virtual
  fuse logic. Never reset into an incomplete encrypted image/profile between
  provisioning phases. Never blindly rerun key burns after an interrupted step.

If a phase fails, preserve the logs/receipt and inspect the real current state.
Resume only the proven remaining phases. Never erase everything, recreate keys
or write a stock image to make an assertion pass.

## 5. Verify and hand over

Verify normal automatic boot without a PIN, Wi-Fi reconnection, settings and file
preservation, all app signatures, both authenticated vault slots, owner USB
access, unauthenticated readout rejection and compatible signed updates. Test
the recovery path without replacing active firmware or settings. Compare the
final live vault backups after any provider token refresh.

Keep the owner's phone workflow: start the device hotspot, connect the phone,
log in with the existing web password, browse SD files and download an ordinary
PCAP. SD files remain unencrypted. The web browser does not need the USB key.
Do not enroll a board managed by the web manager while the compatibility
restriction above applies. A future live validation must test the manager's
private per-device owner profile and existing web password, stock-release
refusal, owner-build checks and explicit package trust/signing.

Return to MENU and lock administration. Supply the owner with backup locations,
key fingerprints, maintenance commands, compatible build/update instructions
and recovery instructions. State exactly what was tested and any live-service
failures. Never imply that a software option alone provides hardware protection
or that an old unsigned flash dump is a usable recovery image after enrollment.

## Hooks for a new app or console command

- Add `device_guard.h`. Keep feature-only state and paths behind
  `CONFIG_TFN_DEVICE_PROTECTION`. OFF hooks must remain empty inline calls.
- A sensitive local setup page calls `device_guard_setup_allowed()` before it
  reads or changes identity, saved credentials, or configuration. Unlock it
  through SETUP > ADMIN ACCESS; never add a separate startup PIN.
- Local internal-file viewers use `device_guard_local_path(path)`. A generic
  viewer must not expose vault plaintext. SD captures remain ordinary files.
- Sensitive console handlers must pass `device_guard_usb_admin()` before any
  read or write, or enter through the central console guard. Do not classify a
  new command as public merely because it is a GET/status command: it can still
  expose identity. Unsupported maintenance requests fail closed.
- AI LIMITS account text uses `device_guard_accounts_read/write`, with the
  `secret` permission and ABI rev 17. `-2` means ordinary firmware and selects
  the app's original storage. Other negative values stop access. Do not use
  this app-specific vault as a general secret store without a storage review.
- Register HTTP handlers through the wrapped `httpd_register_uri_handler`.
  Separate HTTP servers in apps must check their own setup authorization.
- The OS verifies upstream or owner package signatures. Developer mode must
  never bypass verification in a protected build. Do not add keys to firmware
  from a request made by an unauthenticated USB host.

The maintainer integrates these hooks into the MQTT, SERIAL MONITOR, I2C
SCANNER, CLOCK and radio apps added after this feature's starting base.
