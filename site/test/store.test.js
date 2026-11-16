// The checks of the store page logic in Node (no browser, no board): node site/test/store.test.js
// The test makes its own P-256 key, a store index and its signature (the form of sdk/tools/store_index.py).
import { BOARDS } from '../js/boards.js';
import { sha256Hex } from '../js/flasher/core.js';
import {
  appsFor, checkIndex, crc32, decodeIcon, keyId, kindsOf, loadChecked, parseApps, parseCommit, parseStoreSource,
  parseUsbfs, STORE_KEYS, trustedKey, usbTarget, verCmp, verifySig,
} from '../js/store/core.js';
import { BoardLink } from '../js/store/usb.js';

const fails = [];
function expect(cond, what) {
  console.log((cond ? 'PASS ' : 'FAIL ') + what);
  if (!cond) {
    fails.push(what);
  }
}

async function fileEntry(data) {
  return { size: data.length, sha256: await sha256Hex(data) };
}

async function makeIndex(pubHex) {
  const elf = new Uint8Array(500).fill(7);
  const files = {
    'manifest.txt': await fileEntry(new Uint8Array(80).fill(1)),
    'app.elf': await fileEntry(elf),
    sig: await fileEntry(new Uint8Array(64).fill(2)),
  };
  const kind = (id, k) => ({ path: 'apps/' + id + '/' + k + '/', abi: 1, rev: 23, perms: [], bytes: 500, ram: 100,
    files, zip: { path: 'apps/' + id + '/' + id + '-' + k + '.zip', size: 600, sha256: 'a'.repeat(64) } });
  return {
    tfn_store: 1, release: '1.0.0', key_id: (await keyId(pubHex)), pubkey: pubHex, kinds: ['cores3', 'lcd349'],
    apps: [
      { id: 'simon', name: 'SIMON', ver: '1.0.0', code: 1, group: 'GAMES', kinds: { lcd349: kind('simon', 'lcd349') } },
      { id: 'dice', name: 'DICE', ver: '1.0.0', code: 1, group: 'UTIL',
        kinds: { lcd349: kind('dice', 'lcd349'), cores3: kind('dice', 'cores3') } },
    ],
  };
}

const pair = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
const pubHex = Buffer.from(await crypto.subtle.exportKey('raw', pair.publicKey)).toString('hex');
const index = await makeIndex(pubHex);
const data = new TextEncoder().encode(JSON.stringify(index));
const sig = new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, pair.privateKey, data));

expect(checkIndex(index).length === 0, 'a good index has no fault');
expect(await verifySig(data, sig, pubHex), 'the signature of the index is good');
const changed = data.slice();
changed[10] ^= 1;
expect(!(await verifySig(changed, sig, pubHex)), 'a changed index fails the signature');
const other = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
const otherHex = Buffer.from(await crypto.subtle.exportKey('raw', other.publicKey)).toString('hex');
expect(!(await verifySig(data, sig, otherHex)), 'another key fails the signature');
expect(!(await verifySig(data, sig.slice(0, 63), pubHex)), 'a short signature fails');

const bad = JSON.parse(JSON.stringify(index));
bad.apps[0].kinds.lcd349.path = '../../etc/';
expect(checkIndex(bad).length > 0, 'a path out of apps/<id>/<kind>/ is a fault');
const bad2 = JSON.parse(JSON.stringify(index));
delete bad2.apps[1].kinds.cores3.files.sig;
expect(checkIndex(bad2).length > 0, 'a package with no sig is a fault');
const bad3 = JSON.parse(JSON.stringify(index));
bad3.apps[1].kinds.cores3.zip.path = '/abs.zip';
expect(checkIndex(bad3).length > 0, 'a zip path that starts with / is a fault');
expect(checkIndex({ tfn_store: 2 }).length > 0, 'another index form is a fault');

expect(JSON.stringify(appsFor(index, 'lcd349').map((r) => r.app.id)) === '["dice","simon"]',
  'the apps of the 3.49, sorted by name');
expect(appsFor(index, 'cores3').length === 1, 'the apps of the CoreS3: only DICE');
expect(JSON.stringify(kindsOf(index, BOARDS)) === '["lcd349","cores3"]', 'the kinds in the order of boards.js');

const elf = new Uint8Array(500).fill(7);
const fetchBytes = async () => elf;
const got = await loadChecked('apps/dice/lcd349/app.elf', index.apps[1].kinds.lcd349.files['app.elf'], fetchBytes);
expect(got === elf, 'a file with the size and the SHA-256 of the index loads');
let refused = false;
try {
  await loadChecked('x', { size: 500, sha256: 'b'.repeat(64) }, fetchBytes);
} catch (e) {
  refused = true;
}
expect(refused, 'a file with another SHA-256 does not load');

// ---- the key that the page trusts ----
expect(trustedKey(STORE_KEYS[0]) && trustedKey(STORE_KEYS[0].toUpperCase()), 'the official store key is trusted');
expect(!trustedKey(index.pubkey), 'a key that the index brings itself is not trusted');
expect((await keyId(STORE_KEYS[0])) === '1c741c7bab53ac3a', 'the trusted key has the ID of the official store key');

// ---- the icon (TFI1: 2 frames of 2 x 2, 2 colours; index 0 = a hole, index i = colour i - 1) ----
{
  const pal = [0x00f8, 0xe007]; // (red, green: the RGB565 values as the file keeps them, the bytes swapped)
  const head = [0x54, 0x46, 0x49, 0x31, 2, 0, 2, 0, 80, 0, 2, 0];
  const p = pal.flatMap((v) => [v & 255, v >> 8]);
  const f1 = [3, 0, 1, 2, 1]; // a literal run of 4: 0 1 2 1
  const f2 = [253, 2]; // a repeat: 257 - 253 = 4 times 2
  const b = new Uint8Array([...head, ...p, 5, 0, 0, 0, ...f1, 2, 0, 0, 0, ...f2]);
  const ic = decodeIcon(b);
  expect(ic && ic.size === 2 && ic.ms === 80 && ic.frames.length === 2, 'icon: the size, the time and the frames');
  expect(ic && ic.frames[0].join() === '0,1,2,1' && ic.frames[1].join() === '2,2,2,2', 'icon: the runs of the frames');
  expect(ic && ic.rgba[3] === 0 && ic.rgba[4] === 255 && ic.rgba[5] === 0 && ic.rgba[7] === 255 && ic.rgba[9] === 255 &&
    ic.rgba[8] === 0,
    'icon: colour 0 is clear, colour 1 is red, colour 2 is green');
  expect(decodeIcon(new Uint8Array([1, 2, 3])) === null && decodeIcon(b.slice(0, 20)) === null,
    'icon: a bad or short file gives null');
}

// ---- the console answers ----
const appsText = 'I (1) tfn: apps: dice         1.0.118  card state 0 84292 bytes\r\n' +
  'I (1) tfn: apps: clock        1.0.0    board state 0 1000 bytes\r\n' +
  'I (2) tfn: apps: 2 installed, ABI 1, am18, developer mode off\r\n';
const pa = parseApps(appsText);
expect(pa.apps.dice.place === 'card' && pa.apps.clock.ver === '1.0.0' && pa.abi === 1 && pa.kind === 'am18' &&
  pa.count === 2, 'apps: the list, the ABI and the kind');
expect(parseApps('apps: 2 installed, ABI 1, am1').abi === -1, 'apps: a part of the last line is not an answer');
expect(parseStoreSource('apps: store source mgr (now the manager), static store https://a.b/st') === null,
  'store source: a line with no end is not an answer');
expect(parseStoreSource('apps: store source auto (now the static store), static store https://a.b/st/ \r\n').url ===
  'https://a.b/st/', 'store source: the address');
expect(parseCommit('apps: commit dice: ok\r\n', 'dice').ok && !parseCommit('apps: commit dice: FAILED sig\n',
  'dice').ok && parseCommit('apps: commit dice: ok', 'dice') === null, 'commit: ok, FAILED and a line with no end');

function frame(rid, bytes) {
  const hex = Array.from(bytes, (x) => x.toString(16).padStart(2, '0')).join('');
  return 'USBFS1 ' + rid + ' ' + hex + ' ' + crc32(Uint8Array.from(bytes)).toString(16).padStart(8, '0') + '\n';
}

const pf = parseUsbfs(frame('00000007', [0x42]) + frame('00000007', [0x53, 1, 2]), '00000007');
expect(pf && pf[0] === 0x53 && pf.length === 3, 'usbfs: a busy frame is not the answer');
expect(parseUsbfs('USBFS1 00000007 57 00000000\n', '00000007')[0] === 0x21, 'usbfs: a bad CRC is a fault');
expect(usbTarget('dice', true, null).dir === '/apps/.tmp_dice/' &&
  usbTarget('dice', true, { place: 'board' }).dir === '/~board/.tmp_dice/' &&
  usbTarget('x', false, null).commit === 'apps commit x', 'usb: the place of an install');
expect(verCmp('1.0.10', '1.0.9') === 1 && verCmp('1.0', '1.0.0') === 0 && verCmp('1.0.1', '1.1') === -1, 'versions');

// ---- an install over USB with a fake board (the commands of main/usb_fs.inc and main/console_sys.c) ----
function le(v, n) {
  const out = [];
  for (let i = 0; i < n; i++) {
    out.push(v % 256);
    v = Math.floor(v / 256);
  }
  return out;
}

class FakeBoard {
  constructor(opts) {
    this.files = new Map();
    this.card = opts.card;
    this.board = opts.board; // (false = TFN OS 1.0.0: no /~board/)
    this.drop = opts.drop || 0; // (lose this many put lines one time, after the first chunk)
    this.installed = {};
    this.out = [];
  }

  say(s) {
    this.out.push(s + '\r\n');
  }

  reply(id, bytes) {
    this.out.push(frame(id, bytes));
  }

  line(l) {
    if (l === 'ota') {
      this.say('I (1) ota: board am18, firmware 1.0.1, running ota_0');
      return;
    }
    if (l === 'apps') {
      for (const [id, a] of Object.entries(this.installed)) {
        this.say('I (1) tfn: apps: ' + id + ' ' + a.ver + ' ' + a.place + ' state 0 10 bytes');
      }
      this.say('I (1) tfn: apps: ' + Object.keys(this.installed).length + ' installed, ABI 1, am18, developer mode off');
      return;
    }
    if (l.startsWith('apps src')) {
      this.say('I (1) tfn: apps: store source auto (now the static store), static store https://x.y/store/ ');
      return;
    }
    let m = /^apps commit ([a-z0-9_]+)( card)?$/.exec(l);
    if (m) {
      const dir = m[2] ? '/apps/.tmp_' + m[1] + '/' : '/~board/.tmp_' + m[1] + '/';
      const ok = this.files.has(dir + 'sig') && this.files.get(dir + 'sig').length === 64;
      if (ok) {
        this.installed[m[1]] = { ver: '9.9.9', place: m[2] ? 'card' : 'board' };
      }
      this.say('I (1) tfn: apps: commit ' + m[1] + ': ' + (ok ? 'ok' : 'FAILED no signature'));
      return;
    }
    m = /^sdusb ([0-9a-f]{8}) (\S+) ?(\S*) ?(\S*) ?(\S*)$/.exec(l);
    if (!m) {
      return;
    }
    const [, id, op, hex, a1, a2] = m;
    const path = hex ? new TextDecoder().decode(Uint8Array.from(hex.match(/../g), (h) => parseInt(h, 16))) : '';
    const board = path.startsWith('/~board/');
    const text = (s) => Array.from(new TextEncoder().encode(s));
    if (op === 'ls') {
      this.reply(id, text(this.card ? 'L' : '!NO_CARD'));
    } else if (op === 'close') {
      this.reply(id, [0x43]);
    } else if ((board && !this.board) || (!board && !this.card)) {
      this.reply(id, text(board ? '!BAD_REQUEST' : '!NO_CARD'));
    } else if (op === 'put') {
      const off = Number(a1);
      if (this.drop && off > 0) {
        this.drop--;
        return;
      }
      const data = Uint8Array.from(atob(a2), (c) => c.charCodeAt(0));
      const old = off ? this.files.get(path) || new Uint8Array(0) : new Uint8Array(0);
      const f = new Uint8Array(Math.max(old.length, off + data.length));
      f.set(old);
      f.set(data, off);
      this.files.set(path, f);
      this.reply(id, [0x57].concat(le(off + data.length, 8)));
    } else if (op === 'stat') {
      const f = this.files.get(path);
      if (!f) {
        this.reply(id, text('!NOT_FILE'));
        return;
      }
      this.reply(id, [0x42]);
      this.reply(id, [0x53].concat(le(f.length, 8), le(crc32(f), 4)));
    }
  }

  port() {
    const fb = this;
    let ctl = null;
    let buf = '';
    return {
      async open() {
        this.readable = new ReadableStream({
          start(c) {
            ctl = c;
          },
        });
        this.writable = new WritableStream({
          write(chunk) {
            buf += new TextDecoder().decode(chunk);
            let i;
            while ((i = buf.indexOf('\n')) >= 0) {
              const l = buf.slice(0, i).trim();
              buf = buf.slice(i + 1);
              if (l) {
                fb.line(l);
              }
            }
            const out = fb.out.join('');
            fb.out = [];
            if (out) {
              setTimeout(() => ctl.enqueue(new TextEncoder().encode(out)), 1);
            }
          },
        });
      },
      async setSignals() {},
      async close() {},
    };
  }
}

async function usbCase(opts, card) {
  const fb = new FakeBoard(opts);
  const link = new BoardLink(fb.port());
  await link.open();
  const b = await link.hello();
  const files = {
    'manifest.txt': new Uint8Array(300).fill(3),
    'app.elf': new Uint8Array(2000).map((_, i) => (i * 7) & 255),
    sig: new Uint8Array(64).fill(9),
  };
  let err = '';
  try {
    await link.install('dice', files, card, b.apps.dice, () => {});
  } catch (e) {
    err = e.message;
  }
  const dir = card ? '/apps/.tmp_dice/' : '/~board/.tmp_dice/';
  const same = fb.files.has(dir + 'app.elf') && crc32(fb.files.get(dir + 'app.elf')) === crc32(files['app.elf']);
  return { b, err, same, fb };
}

{
  const r = await usbCase({ card: true, board: true }, true);
  expect(r.b.kind === 'am18' && r.b.card && r.b.store.url === 'https://x.y/store/', 'usb: the facts of the board');
  expect(!r.err && r.same && r.fb.installed.dice.place === 'card', 'usb: an install on the SD card');
  const r2 = await usbCase({ card: false, board: true }, false);
  expect(!r2.b.card && !r2.err && r2.same && r2.fb.installed.dice.place === 'board',
    'usb: an install in the board storage');
  const r3 = await usbCase({ card: false, board: false }, false);
  expect(/BAD_REQUEST/.test(r3.err) && !r3.fb.installed.dice, 'usb: TFN OS 1.0.0 with no card refuses the board path');
  const r4 = await usbCase({ card: true, board: true, drop: 1 }, true);
  expect(!r4.err && r4.same, 'usb: a lost put line goes again, and the file is the same');
}

console.log(fails.length ? 'STORE PAGE TESTS FAILED: ' + fails.length : 'STORE PAGE TESTS OK');
process.exit(fails.length ? 1 : 0);
