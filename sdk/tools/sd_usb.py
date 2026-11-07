#!/usr/bin/env python3
"""List and download SD-card files over the board's USB serial console.

Install pyserial in the Python environment first: python -m pip install pyserial.
Examples:
    python sdk/tools/sd_usb.py --port <serial-port> ls /WIFI_TEST
    python sdk/tools/sd_usb.py --port <serial-port> get /WIFI_TEST/Lab_00000000.pcap --out capture.pcap
    python sdk/tools/sd_usb.py --port <serial-port> pull /WIFI_TEST --out captures
"""
import argparse
from dataclasses import dataclass
import errno
import fnmatch
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
import tempfile
import time
import zlib


class TransferError(Exception):
    pass


@dataclass(frozen=True)
class Entry:
    name: str
    directory: bool
    size: int


def remote_path(value):
    if (not value.startswith('/') or '\\' in value or ':' in value or
            any(ord(c) < 32 or ord(c) == 127 for c in value) or
            any(p in ('.', '..') for p in value.split('/')) or '//' in value):
        raise TransferError('Use an absolute path inside the SD card, without . or .. components.')
    value = value.rstrip('/') or '/'
    if len(value.encode('utf-8')) > 239:
        raise TransferError('SD path exceeds 239 UTF-8 bytes.')
    return value


class Client:
    def __init__(self, port, timeout=5.0, retries=4, *, command='sdusb', authorize=None):
        self.port, self.timeout, self.retries = port, timeout, retries
        self._command = command
        self.authorize = authorize
        self.sequence = 0
        self.pending = bytearray()
        reply = self.request('ping')
        if len(reply) != 3 or reply[:1] != b'P':
            raise TransferError('Unsupported SD USB protocol.')
        self.chunk_size = struct.unpack('<H', reply[1:])[0]
        if not 1 <= self.chunk_size <= 512:
            raise TransferError('Invalid USB chunk size.')

    def request(self, op, path=None, *values):
        for _ in range(self.retries):
            if self.authorize:
                self.authorize()
            self.sequence = (self.sequence + 1) & 0xffffffff or 1
            identifier = f'{self.sequence:08x}'
            fields = [self._command, identifier, op]
            if path is not None:
                fields.append(remote_path(path).encode('utf-8').hex())
            fields.extend(str(value) for value in values)
            self.port.write((' '.join(fields) + '\r\n').encode('ascii'))
            deadline = time.monotonic() + self.timeout
            renew = False
            while time.monotonic() < deadline:
                waiting = getattr(self.port, 'in_waiting', 0)
                chunk = self.port.read(min(4096, waiting) if waiting else 1)
                if not chunk:
                    continue
                self.pending.extend(chunk)
                while b'\n' in self.pending:
                    line, _, remaining = self.pending.partition(b'\n')
                    self.pending = bytearray(remaining)
                    if b'ADMIN REQUIRED' in line:
                        if not self.authorize:
                            raise TransferError('This board requires owner USB authentication. Use --admin-key.')
                        renew = True
                        break
                    # Other firmware tasks can log at any time. Only CRC-checked
                    # replies for this request count; all other lines are ignored.
                    match = re.search(rb'USBFS1 ' + identifier.encode() + rb' ([0-9a-f]+) ([0-9a-f]{8})\r?$', line)
                    if not match or len(match[1]) > 1042:
                        continue
                    try:
                        payload = bytes.fromhex(match[1].decode('ascii'))
                    except ValueError:
                        continue
                    if not payload or zlib.crc32(payload) != int(match[2], 16):
                        continue
                    if payload == b'B' and op == 'stat':
                        deadline = time.monotonic() + self.timeout
                        continue
                    if payload[:1] == b'!':
                        if self.authorize and payload == b'!ADMIN_REQUIRED':
                            renew = True
                            break
                        raise TransferError('Device: ' + payload[1:].decode('ascii', 'replace'))
                    return payload
                if renew:
                    invalidate = getattr(self.authorize, 'invalidate', None)
                    if invalidate:
                        invalidate()
                    self.authorize()
                    break
                if len(self.pending) > 16384:
                    self.pending.clear()
        raise TransferError('No valid SD USB reply after retries. Check the port and install firmware with sdusb support.')

    def entries(self, path):
        path = remote_path(path)
        for index in range(100000):
            reply = self.request('ls', path, index)
            if reply == b'E':
                return
            if len(reply) < 11 or reply[:1] != b'L' or reply[1:2] not in (b'D', b'F'):
                raise TransferError('Invalid directory reply.')
            try:
                name = reply[10:].decode('utf-8')
            except UnicodeError as error:
                raise TransferError('Invalid UTF-8 filename from device.') from error
            if not name or name in ('.', '..') or '/' in name or '\\' in name or '\0' in name:
                raise TransferError('Invalid filename from device.')
            yield Entry(name, reply[1:2] == b'D', struct.unpack('<Q', reply[2:10])[0])
        raise TransferError('Directory exceeds 100000 entries.')

    def info(self, path):
        reply = self.request('stat', path)
        if len(reply) != 13 or reply[:1] != b'S':
            raise TransferError('Invalid file information reply.')
        return struct.unpack('<QI', reply[1:])

    def download(self, path, target, overwrite=False):
        path, target = remote_path(path), Path(target)
        size, expected_crc = self.info(path)
        if target.exists():
            if target.is_file() and file_info(target) == (size, expected_crc):
                return 'verified existing', size
            if not overwrite:
                raise TransferError(f'{target} already exists with different contents; use --overwrite to replace it.')
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix='.sd-usb-', suffix='.part', dir=target.parent)
        temporary = Path(temporary)
        crc, offset = 0, 0
        try:
            with os.fdopen(fd, 'wb') as output:
                while offset < size:
                    length = min(self.chunk_size, size - offset)
                    reply = self.request('get', path, offset, length)
                    if (len(reply) != length + 9 or reply[:1] != b'R' or
                            struct.unpack('<Q', reply[1:9])[0] != offset):
                        raise TransferError('Invalid file chunk offset or length.')
                    data = reply[9:]
                    output.write(data)
                    crc = zlib.crc32(data, crc)
                    offset += length
            if offset != size or crc != expected_crc:
                raise TransferError('File checksum mismatch. The source may have changed; save the capture and retry.')
            if overwrite:
                os.replace(temporary, target)
            else:
                install_new_file(temporary, target)
            return 'downloaded', size
        finally:
            temporary.unlink(missing_ok=True)


def file_info(path):
    crc, size = 0, 0
    with Path(path).open('rb') as source:
        while block := source.read(65536):
            crc = zlib.crc32(block, crc)
            size += len(block)
    return size, crc


def install_new_file(source, target):
    """Publish verified content without replacing a file created during transfer."""
    try:
        os.link(source, target)
    except OSError as error:
        if error.errno not in (errno.EPERM, errno.EACCES, errno.ENOTSUP, errno.EXDEV):
            raise
        # Some destination filesystems cannot hard-link. Exclusive creation still
        # preserves pre-existing files. Remove only this new output on copy failure.
        with target.open('xb') as output:
            try:
                with source.open('rb') as data:
                    shutil.copyfileobj(data, output)
            except BaseException:
                output.close()
                target.unlink()
                raise


def local_name(name):
    stem = name.split('.', 1)[0].upper()
    reserved = {'CON', 'PRN', 'AUX', 'NUL'} | {f'{prefix}{i}' for prefix in ('COM', 'LPT') for i in range(1, 10)}
    if (name in ('', '.', '..') or name[-1:] in (' ', '.') or
            any(ord(c) < 32 or c in '<>:"/\\|?*' for c in name) or stem in reserved):
        raise TransferError('Filename cannot be saved automatically; use get with an explicit --out filename.')
    return name


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--timeout', type=float, default=5.0)
    parser.add_argument('--retries', type=int, default=4)
    parser.add_argument('--admin-key', type=Path, help='Owner USB administration key for a protected board')
    commands = parser.add_subparsers(dest='command', required=True)
    listing = commands.add_parser('ls', help='List an SD card directory')
    listing.add_argument('path', nargs='?', default='/')
    get = commands.add_parser('get', help='Download one file')
    get.add_argument('path')
    get.add_argument('--out', type=Path)
    get.add_argument('--overwrite', action='store_true')
    pull = commands.add_parser('pull', help='Download matching files from a directory')
    pull.add_argument('path', nargs='?', default='/WIFI_TEST')
    pull.add_argument('--out', type=Path, default=Path('captures'))
    pull.add_argument('--pattern', default='*.pcap')
    pull.add_argument('--overwrite', action='store_true')
    args = parser.parse_args()
    if args.timeout <= 0 or args.retries < 1:
        parser.error('--timeout must be positive and --retries must be at least 1')
    try:
        import serial
    except ImportError:
        parser.exit(1, 'Install pyserial first: python -m pip install pyserial\n')
    try:
        path = remote_path(args.path)
        port = serial.Serial()
        port.port, port.baudrate, port.timeout = args.port, args.baud, min(0.1, args.timeout)
        port.write_timeout, port.dtr, port.rts = 5, False, False
        with port:
            port.reset_input_buffer()
            authorize = None
            if args.admin_key:
                sys.modules.setdefault('sd_usb', sys.modules[__name__])
                from usb_owner import Authorization
                authorize = Authorization(port, args.admin_key)
            client = Client(port, args.timeout, args.retries, authorize=authorize)
            if args.command == 'ls':
                for entry in client.entries(path):
                    print(f'{"DIR" if entry.directory else "FILE":4} {entry.size:12} {entry.name}')
            elif args.command == 'get':
                target = args.out or Path(local_name(PurePosixPath(path).name))
                action, size = client.download(path, target, args.overwrite)
                print(f'{action}: {target} ({size} bytes, CRC32 verified)')
            else:
                count = total = 0
                # Snapshot names before downloading so long transfers do not change
                # directory indices. Temporary captures are excluded by *.pcap.
                entries = list(client.entries(path))
                for entry in entries:
                    if entry.directory or not fnmatch.fnmatchcase(entry.name.lower(), args.pattern.lower()):
                        continue
                    target = args.out / local_name(entry.name)
                    action, size = client.download(path.rstrip('/') + '/' + entry.name, target, args.overwrite)
                    print(f'{action}: {target} ({size} bytes, CRC32 verified)')
                    count += 1
                    total += size
                print(f'{count} files, {total} bytes; originals remain on the SD card.')
    except (TransferError, OSError, serial.SerialException) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
