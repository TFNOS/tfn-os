"""Bounded, offline FAT/WL preservation and AI account migration for enrollment."""

from pathlib import PurePosixPath
import re
import sys
from types import SimpleNamespace
from device_image import idf_path, sha, vault_file


def safe_name(name):
    if (
        not name
        or name in (".", "..")
        or any(character in name for character in "/\\:\0")
        or any(ord(character) < 32 for character in name)
        or len(name.encode("utf-8")) > 255
        or name.endswith((" ", "."))
    ):
        raise ValueError("Unsafe or unsupported FAT name")
    return name


def read_fat(volume):
    """Read IDF FAT structures with explicit chain, depth and path bounds."""
    sys.path.insert(0, str(idf_path() / "components/fatfs"))
    import construct
    import fatfsparse as parser
    from fatfs_utils.cluster import Cluster

    parser.args = SimpleNamespace(long_name_support=True)
    raw = parser.remove_wear_levelling_if_exists(volume)
    boot = parser.BootSector()
    boot.parse_boot_sector(raw)
    state = boot.boot_sector_state
    if state.sector_size != 4096 or state.sectors_per_cluster != 1:
        raise ValueError("Enrollment supports the stock 4096-byte FAT allocation unit only")
    fat = parser.FAT(state, init_=False)
    files = {}
    directories = set()
    names = set()

    def content(cluster, size=None):
        result = bytearray()
        seen = set()
        while True:
            if cluster < 2 or cluster in seen or len(seen) >= len(raw) // 4096:
                raise ValueError("Invalid or cyclic FAT chain")
            seen.add(cluster)
            offset = Cluster.compute_cluster_data_address(state, cluster)
            if offset < 0 or offset + 4096 > len(raw):
                raise ValueError("FAT chain leaves the partition")
            result.extend(raw[offset : offset + 4096])
            if fat.is_cluster_last(cluster):
                break
            cluster = fat.get_cluster_value(cluster)
        if size is not None and size > len(result):
            raise ValueError("File size exceeds its FAT chain")
        return bytes(result if size is None else result[:size])

    def walk(data, parent, ancestors):
        if len(parent) > 20 or len(data) % 32:
            raise ValueError("Unsupported FAT directory depth or size")
        for index in range(len(data) // 32):
            entry_bytes = data[index * 32 : (index + 1) * 32]
            if entry_bytes[0] == 0:
                break
            if entry_bytes[0] == 0xE5 or entry_bytes[11] == 0x0F:
                continue
            try:
                entry = parser.Entry.ENTRY_FORMAT_SHORT_NAME.parse(entry_bytes)
            except (construct.core.ConstError, UnicodeDecodeError, construct.core.StringError) as error:
                raise ValueError("Corrupt FAT directory entry") from error
            attribute = entry["DIR_Attr"]
            if attribute & 8:
                continue
            name = parser.get_obj_name(
                entry, data, index, parser.lfn_checksum(entry["DIR_Name"] + entry["DIR_Name_ext"])
            )
            # FatFs stores lowercase 8.3 names in the NT-reserved flags rather
            # than an LFN record. IDF's parser leaves those names uppercase.
            flags = entry["DIR_NTRes"]
            if flags & 0x18:
                base, dot, extension = name.rpartition(".")
                if not dot:
                    base, extension = name, ""
                if flags & 0x08:
                    base = base.lower()
                if flags & 0x10:
                    extension = extension.lower()
                name = base + ("." + extension if dot else "")
            if name in (".", "..") and attribute & parser.Entry.ATTR_DIRECTORY:
                continue
            safe_name(name)
            path = "/".join(parent + [name])
            if len(path.encode("utf-8")) > 239 or path.casefold() in names or len(names) >= 4096:
                raise ValueError("Duplicate or oversized FAT path")
            names.add(path.casefold())
            cluster = parser.Entry.get_cluster_id(entry)
            if attribute & parser.Entry.ATTR_DIRECTORY:
                if cluster in ancestors:
                    raise ValueError("Cyclic FAT directory")
                directories.add(path)
                walk(content(cluster), parent + [name], ancestors | {cluster})
            else:
                size = entry["DIR_FileSize"]
                files[path] = content(cluster, size) if size else b""

    offset = state.root_directory_start
    length = state.root_dir_sectors_cnt * state.sector_size
    walk(raw[offset : offset + length], [], set())
    return files, directories


def write_fat(files, directories, size):
    """Use the IDF generator, then independently parse and compare every file."""
    sys.path.insert(0, str(idf_path() / "components/fatfs"))
    from wl_fatfsgen import WLFATFS
    from fatfs_utils import fs_object

    volume = WLFATFS(size=size, sector_size=4096, sectors_per_cluster=1, long_names_enabled=True)
    preserve_case(volume.plain_fatfs.root_directory, fs_object)
    for path in sorted(directories, key=lambda name: (name.count("/"), name)):
        parts = list(PurePosixPath(path).parts)
        for name in parts:
            safe_name(name)
        volume.plain_fatfs.create_directory(parts[-1], path_from_root=parts[:-1])
        child = volume.plain_fatfs.root_directory.recursive_search(parts, volume.plain_fatfs.root_directory)
        preserve_case(child, fs_object)
    for path, data in sorted(files.items()):
        parts = list(PurePosixPath(path).parts)
        for name in parts:
            safe_name(name)
        leaf = parts[-1]
        base, dot, extension = leaf.rpartition(".")
        if not dot or not base:
            base, extension = leaf, ""
        volume.plain_fatfs.create_file(base, extension, parts[:-1], is_empty=not data)
        if data:
            volume.plain_fatfs.write_content(parts, data)
    volume.init_wl()
    result = bytes(volume.fatfs_binary_image)
    checked_files, checked_dirs = read_fat(result)
    if len(result) != size or checked_files != files or checked_dirs != directories:
        raise ValueError("FAT readback changed a file or directory; no image accepted")
    return result


def preserve_case(directory, helpers):
    """Per-directory IDF 6.1 adapter; do not change process-global methods.

    The IDF generator expects uppercase short names and lowercases its LFN
    blocks. Use its allocation/checksum helpers but preserve the UTF-16 bytes.
    The allocation sequence follows Espressif fs_object.py (Apache-2.0,
    Copyright 2021-2026 Espressif Systems (Shanghai) CO LTD).
    """
    short_names = set()

    def valid_sfn(name, extension):
        # IDF marks generated short names with NTRes=0x18. FatFs interprets
        # that as lowercase. Use an LFN even for uppercase 8.3 user names.
        return False

    def allocate(free_entry, name, extension, target_dir, free_cluster_id, entity_type, date, time):
        full = helpers.build_lfn_full_name(name, extension)
        # IDF's short-name helper does not bound non-ASCII names by encoded
        # byte count. Give each long name an ASCII alias and check collisions.
        digest = sha(full.encode("utf-16le")).upper()
        suffix = re.sub("[^A-Z0-9_]", "_", extension.upper())[:3].ljust(3)
        for number in range(1, 4097):
            digits = str(number)
            alias = "T" + digest[: 6 - len(digits)] + "~" + digits
            short = alias + suffix
            if short not in short_names:
                short_names.add(short)
                break
        else:
            raise ValueError("No unique FAT short alias")
        checksum = helpers.lfn_checksum(short)
        count = helpers.get_required_lfn_entries_count(full)
        pieces = helpers.split_name_to_lfn_entries(full, count)
        for index, piece in reversed(list(enumerate(pieces))):
            free_entry.allocate_entry(
                first_cluster_id=free_cluster_id,
                entity_name=name,
                entity_extension=extension,
                entity_type=entity_type,
                lfn_order=index + 1,
                lfn_names=helpers.split_name_to_lfn_entry_blocks(piece),
                lfn_checksum_=checksum,
                lfn_is_last=index + 1 == count,
            )
            free_entry = target_dir.find_free_entry() or target_dir.chain_directory()
        free_entry.allocate_entry(
            first_cluster_id=free_cluster_id,
            entity_name=short[: helpers.MAX_NAME_SIZE],
            entity_extension=short[helpers.MAX_NAME_SIZE :],
            entity_type=entity_type,
            lfn_order=helpers.Entry.SHORT_ENTRY_LN,
            date=date,
            time=time,
        )
        return free_entry

    directory._is_valid_sfn = valid_sfn
    directory.allocate_long_name_object = allocate


def migrate_accounts(files, master):
    """Convert the legacy account pair only after verified authenticated writes."""
    aliases = {name.casefold(): name for name in files}
    if any(name in aliases for name in ("ai_accounts.enc", "ai_accounts.tmp")):
        raise ValueError("An existing encrypted vault requires its original owner recovery workflow")
    choices = [aliases[name] for name in ("ai_limits.key", "ai_limits.kew") if name in aliases]
    if not choices:
        return set()
    valid = next(
        (
            files[name]
            for name in choices
            if files[name].startswith(b"AILIM-ACCT 1\n") and len(files[name]) <= 12288
        ),
        None,
    )
    if valid is None:
        raise ValueError("Legacy AI account files could not be verified")
    replacements = {
        "ai_accounts.enc": vault_file(valid, master, 1),
        "ai_accounts.tmp": vault_file(valid, master, 2),
    }
    files.update(replacements)
    for name in choices:
        del files[name]
    return set(choices) | set(replacements)


def sign_installed(files, key, ai_package=None):
    """Trust existing installed code explicitly, after checking its manifest hashes."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils

    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Installed apps require an owner P-256 app-signing key")
    changed = set()
    roots = sorted(
        name[:-13] for name in files if re.fullmatch(r"apps/[a-z0-9_]{1,20}/manifest\.txt", name, re.I)
    )
    for root in roots:
        aliases = {name.casefold(): name for name in files}
        manifest_name = aliases[(root + "/manifest.txt").casefold()]
        raw = files[manifest_name]
        fields = dict(line.split("=", 1) for line in raw.decode("utf-8").splitlines() if "=" in line)
        if fields.get("id") != root.split("/")[-1].lower() or fields.get("kind") != "lcd349":
            raise ValueError("Installed app identity or kind is invalid")
        if fields["id"] == "ai_limits":
            if ai_package is None:
                raise ValueError("Supply the vault-capable AI LIMITS package before migrating its accounts")
            for name in ("app.elf", "icon.bin", "manifest.txt"):
                target = aliases.get((root + "/" + name).casefold(), root + "/" + name)
                files[target] = (ai_package / name).read_bytes()
                changed.add(target)
            raw = files[manifest_name]
            fields = dict(line.split("=", 1) for line in raw.decode("utf-8").splitlines() if "=" in line)
            if (
                fields.get("id") != "ai_limits"
                or fields.get("kind") != "lcd349"
                or int(fields.get("rev", "0")) < 17
            ):
                raise ValueError("AI package must be built against the protected vault ABI")
        for name, field in [
            ("app.elf", "elf_sha256"),
            ("icon.bin", "icon_sha256"),
            ("watch.elf", "watch_sha256"),
        ]:
            if name == "watch.elf" and not fields.get(field):
                continue
            content = files.get(aliases.get((root + "/" + name).casefold(), root + "/" + name))
            if content is None or sha(content) != fields.get(field):
                raise ValueError("Installed app content differs from its manifest")
        r, s = utils.decode_dss_signature(key.sign(raw, ec.ECDSA(hashes.SHA256())))
        signature_name = aliases.get((root + "/sig").casefold(), root + "/sig")
        files[signature_name] = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        changed.add(signature_name)
    return changed
