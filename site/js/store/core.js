// The logic of the store page with no browser parts: check the form of the store index (sdk/tools/store_index.py
// makes it), check its signature, list the apps of one board kind, and check a file before the download. The tests
// (test/store.test.js) run this file in Node.
import { sha256Hex } from '../flasher/core.js';

export const STORE_FORMAT = 1;
export const NEED = ['manifest.txt', 'app.elf', 'sig'];
export const FILES = ['manifest.txt', 'app.elf', 'icon.bin', 'sig', 'watch.elf'];
const ID = /^[a-z0-9_]{1,23}$/;
const KIND = /^[a-z0-9]{1,15}$/;
const HEX64 = /^[0-9a-f]{64}$/;

function safePath(p) {
  return typeof p === 'string' && p.length > 0 && !p.startsWith('/') && !p.includes('\\') &&
    !p.replace(/\/$/, '').split('/').some((x) => x === '' || x === '.' || x === '..');
}

function fileOk(v) {
  return !!v && Number.isInteger(v.size) && v.size > 0 && HEX64.test(v.sha256 || '');
}

// The faults of the form of the index (the same checks as check_index_form of store_index.py). [] = good.
export function checkIndex(index) {
  const errors = [];
  if (!index || index.tfn_store !== STORE_FORMAT) {
    return ['the file is not a store index of form ' + STORE_FORMAT];
  }
  if (!/^[0-9a-f]{16}$/.test(index.key_id || '') || !/^04[0-9a-f]{128}$/.test(index.pubkey || '')) {
    errors.push('the index has no key');
  }
  if (!Array.isArray(index.apps) || !index.apps.length) {
    return errors.concat('the index has no apps');
  }
  const seen = new Set();
  for (const a of index.apps) {
    if (!a || !ID.test(a.id || '') || seen.has(a.id) || typeof a.name !== 'string' || typeof a.ver !== 'string' ||
        !a.kinds || typeof a.kinds !== 'object') {
      errors.push('an app with no id, name, version or kinds');
      continue;
    }
    seen.add(a.id);
    for (const [kind, e] of Object.entries(a.kinds)) {
      const where = a.id + '/' + kind;
      if (!KIND.test(kind) || !e || e.path !== 'apps/' + a.id + '/' + kind + '/') {
        errors.push(where + ': the path is not apps/<id>/<kind>/');
        continue;
      }
      const names = Object.keys(e.files || {});
      if (NEED.some((f) => !names.includes(f)) || names.some((f) => !FILES.includes(f))) {
        errors.push(where + ': the file list is not good');
      }
      if (names.some((f) => !fileOk(e.files[f]))) {
        errors.push(where + ': a file has no size or SHA-256');
      }
      if (!fileOk(e.zip) || !safePath(e.zip.path)) {
        errors.push(where + ': the zip has no path, size or SHA-256');
      }
    }
  }
  return errors;
}

// The apps of one board kind, sorted by name: [{ app, entry }].
export function appsFor(index, kind) {
  return index.apps.filter((a) => a.kinds[kind]).map((a) => ({ app: a, entry: a.kinds[kind] }))
    .sort((x, y) => x.app.name.localeCompare(y.app.name));
}

// The board kinds of the index, in the order of the list of known boards (boards.js), then the others.
export function kindsOf(index, boards) {
  const have = new Set(index.apps.flatMap((a) => Object.keys(a.kinds)));
  const known = boards.filter((b) => have.has(b.kind)).map((b) => b.kind);
  return known.concat([...have].filter((k) => !known.includes(k)).sort());
}

function hexBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(hex.substr(i * 2, 2), 16);
  }
  return out;
}

// True: sig (64 bytes, r || s) is a good ECDSA P-256 / SHA-256 signature of data by the key pubHex (130 hex digits).
// The form of WebCrypto ECDSA is r || s: the same as the file "sig" of the board.
export async function verifySig(data, sig, pubHex) {
  if (!sig || sig.length !== 64 || !/^04[0-9a-f]{128}$/.test(pubHex || '')) {
    return false;
  }
  try {
    const key = await crypto.subtle.importKey('raw', hexBytes(pubHex), { name: 'ECDSA', namedCurve: 'P-256' }, false,
      ['verify']);
    return await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, sig, data);
  } catch (e) {
    return false;
  }
}

// The key ID of a public key: the first 8 bytes of its SHA-256 (main/tfn_pub.h).
export async function keyId(pubHex) {
  return (await sha256Hex(hexBytes(pubHex))).slice(0, 16);
}

// Get a file of the store and check its size and its SHA-256. fetchBytes(path) gives a Uint8Array.
export async function loadChecked(path, want, fetchBytes) {
  const data = await fetchBytes(path + '?sha=' + want.sha256.slice(0, 12));
  if (data.length !== want.size) {
    throw new Error(path + ': the file has ' + data.length + ' bytes, the index says ' + want.size);
  }
  if ((await sha256Hex(data)) !== want.sha256) {
    throw new Error(path + ': the SHA-256 is not the same as in the index');
  }
  return data;
}

// The size of a number of bytes for a person.
export function sizeText(n) {
  return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
}

// ---- the keys that the page trusts ----

// The public keys of the official store (tfn_keys: store.pem, key ID 1c741c7bab53ac3a). The index must name one of
// these keys, and its signature must be good with it: a key that the index brings itself proves nothing. A fork of
// TFN OS with its own store key puts its key here.
export const STORE_KEYS = [
  '04d440c38c71022f6d9f31e97433b053a65b7df5f9e284e70cac48ae82e6ff930b18e9351516ec5d3a1c2d70bbcd3a023355b0b257a5b49c78fb1fe62baae20d85',
];

export function trustedKey(pubHex, keys = STORE_KEYS) {
  return keys.includes((pubHex || '').toLowerCase());
}

// ---- the menu icon of an app (icon.bin, form TFI1: sdk/tools/icon_png.py) ----

// -> { size, ms, frames: [Uint8Array of indexes, size * size each], rgba: Uint8ClampedArray(4 * (colours + 1)) }
// Index 0 is clear (the hole of the icon); index i is colour i - 1 of the palette of the file (tfn_pkg.icon_write).
// A file that is not good gives null.
export function decodeIcon(b) {
  if (!b || b.length < 12 || b[0] !== 0x54 || b[1] !== 0x46 || b[2] !== 0x49 || b[3] !== 0x31) {
    return null;
  }
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const size = dv.getUint16(4, true);
  const nf = dv.getUint16(6, true);
  const ms = dv.getUint16(8, true);
  const nc = dv.getUint16(10, true);
  if (!size || size > 256 || !nf || nf > 64 || !nc || nc > 256 || 12 + 2 * nc > b.length) {
    return null;
  }
  const rgba = new Uint8ClampedArray(4 * (nc + 1));
  for (let i = 0; i < nc; i++) {
    const raw = dv.getUint16(12 + 2 * i, true);
    const v = ((raw >> 8) | (raw << 8)) & 0xffff;
    const o = 4 * (i + 1);
    rgba[o] = Math.round((((v >> 11) & 31) * 255) / 31);
    rgba[o + 1] = Math.round((((v >> 5) & 63) * 255) / 63);
    rgba[o + 2] = Math.round(((v & 31) * 255) / 31);
    rgba[o + 3] = 255;
  }
  const frames = [];
  let p = 12 + 2 * nc;
  for (let f = 0; f < nf; f++) {
    if (p + 4 > b.length) {
      return null;
    }
    const len = dv.getUint32(p, true);
    p += 4;
    const end = p + len;
    if (end > b.length) {
      return null;
    }
    const idx = new Uint8Array(size * size);
    let n = 0;
    let q = p;
    while (q < end && n < idx.length) {
      const c = b[q++];
      if (c < 128) {
        for (let k = 0; k <= c && q < end && n < idx.length; k++) {
          idx[n++] = b[q++];
        }
      } else {
        const v = b[q++];
        for (let k = 0; k < 257 - c && n < idx.length; k++) {
          idx[n++] = v;
        }
      }
    }
    frames.push(idx);
    p = end;
  }
  return { size, ms: ms || 100, frames, rgba };
}

// ---- the USB console of a board (the install over USB) ----

// (The parsers take only whole lines: the text of the console comes in parts.)
// The answer of the console command "apps": the installed apps and the facts of the board.
// -> { apps: { <id>: { ver, place: 'card' | 'board' } }, count, abi, kind } (abi -1 when the line is not there)
export function parseApps(text) {
  const apps = {};
  const out = { apps, count: -1, abi: -1, kind: '' };
  for (const m of (text || '').matchAll(/apps: ([a-z0-9_]{1,23}) +(\S+) +(card|board) state -?\d+ \d+ bytes/g)) {
    apps[m[1]] = { ver: m[2], place: m[3] };
  }
  const s = /apps: (\d+) installed, ABI (\d+), ([a-z0-9]+),/.exec(text || '');
  if (s) {
    out.count = Number(s[1]);
    out.abi = Number(s[2]);
    out.kind = s[3];
  }
  return out;
}

// The answer of "apps src": the address of the static store of the board ('' when the line is not there).
export function parseStoreSource(text) {
  const m = /apps: store source (auto|mgr|static) \(now [^)]*\), static store (https:\/\/[^\s]+)[^\r\n]*[\r\n]/.exec(text || '');
  return m ? { mode: m[1], url: m[2] } : null;
}

// The answer of "apps commit <id>": null (no answer yet) or { ok, msg }.
export function parseCommit(text, id) {
  const re = new RegExp('apps: commit ' + id + ': (ok|FAILED) ?([^\r\n]*)[\r\n]');
  const m = re.exec(text || '');
  return m ? { ok: m[1] === 'ok', msg: m[2].trim() } : null;
}

export function crc32(data) {
  let crc = ~0 >>> 0;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i];
    for (let k = 0; k < 8; k++) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return ~crc >>> 0;
}

// The answer frame of "sdusb": "USBFS1 <id> <hex payload> <crc>" (main/usb_fs.inc). -> the payload, a Uint8Array
// starting with '!' for a fault of the frame, or null when the text has no answer for this id yet. A "B" frame (busy:
// the board reads a big file) is not the answer.
export function parseUsbfs(text, rid) {
  for (const m of (text || '').matchAll(new RegExp('USBFS1 ' + rid + ' ([0-9a-f]*) ([0-9a-f]{8})', 'g'))) {
    const hex = m[1];
    const p = new Uint8Array(hex.length / 2);
    for (let i = 0; i < p.length; i++) {
      p[i] = parseInt(hex.substr(2 * i, 2), 16);
    }
    if (crc32(p) !== parseInt(m[2], 16)) {
      return new TextEncoder().encode('!CRC');
    }
    if (p.length === 1 && p[0] === 0x42) {
      continue;
    }
    return p;
  }
  return null;
}

export function hexText(s) {
  return Array.from(new TextEncoder().encode(s), (c) => c.toString(16).padStart(2, '0')).join('');
}

export function base64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) {
    s += String.fromCharCode(bytes[i]);
  }
  return btoa(s);
}

// A little-endian number of n bytes at p.
export function leNum(b, p, n) {
  let v = 0;
  for (let i = n - 1; i >= 0; i--) {
    v = v * 256 + b[p + i];
  }
  return v;
}

// Where an app goes over USB: the folder of the files (a path of "sdusb put") and the word of "apps commit".
// The SD card when the board has one (or when the app is there now), else the board storage (TFN OS 1.0.1 and
// later: /~board/ in main/usb_fs.inc).
export function usbTarget(id, card, installed) {
  const onCard = installed ? installed.place === 'card' : card;
  return onCard ? { dir: '/apps/.tmp_' + id + '/', commit: 'apps commit ' + id + ' card', card: true }
    : { dir: '/~board/.tmp_' + id + '/', commit: 'apps commit ' + id, card: false };
}

// Compare two versions "1.0.12" -> -1, 0, 1.
export function verCmp(a, b) {
  const x = String(a || '0').split('.').map(Number);
  const y = String(b || '0').split('.').map(Number);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    const d = (x[i] || 0) - (y[i] || 0);
    if (d) {
      return d < 0 ? -1 : 1;
    }
  }
  return 0;
}
