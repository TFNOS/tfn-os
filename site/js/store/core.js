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
