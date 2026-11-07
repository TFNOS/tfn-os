// The checks of the store page logic in Node (no browser, no board): node site/test/store.test.js
// The test makes its own P-256 key, a store index and its signature (the form of sdk/tools/store_index.py).
import { BOARDS } from '../js/boards.js';
import { sha256Hex } from '../js/flasher/core.js';
import { appsFor, checkIndex, keyId, kindsOf, loadChecked, verifySig } from '../js/store/core.js';

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

console.log(fails.length ? 'STORE PAGE TESTS FAILED: ' + fails.length : 'STORE PAGE TESTS OK');
process.exit(fails.length ? 1 : 0);
