// The flasher checks in Node (no browser, no board): node site/test/flasher.test.js
import { createHash } from 'node:crypto';
import { md5Hex } from '../js/flasher/md5.js';
import { runAll } from './run.js';

function md5Checks() {
  const fails = [];
  for (const n of [0, 1, 55, 56, 63, 64, 65, 1000, 70001]) {
    const data = new Uint8Array(n).map((_, i) => (i * 31 + 7) & 255);
    const want = createHash('md5').update(data).digest('hex');
    if (md5Hex(data) !== want) {
      fails.push('md5 of ' + n + ' bytes');
    }
  }
  return fails;
}

const results = await runAll();
results.push({ name: 'md5', label: 'MD5 of the write check', fails: md5Checks() });
let bad = 0;
for (const r of results) {
  if (r.fails.length) {
    bad++;
  }
  const why = r.fails.length ? ' -- ' + r.fails.join('; ') : '';
  console.log((r.fails.length ? 'FAIL ' : 'PASS ') + r.name + ': ' + r.label + why);
}
console.log(bad ? 'FLASHER TESTS FAILED: ' + bad : 'FLASHER TESTS OK (' + results.length + ')');
process.exit(bad ? 1 : 0);
