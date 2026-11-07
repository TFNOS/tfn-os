// The checks of the flasher with the fake port and the fake ROM bootloader. test.html and flasher.test.js run them.
// The firmware files here are made-up bytes in the layout of the real manifests (tools/fetch_firmware.py).
import { BOARDS, boardByKind } from '../js/boards.js';
import { buildPlan, hex, loadPlanFiles, sha256Hex } from '../js/flasher/core.js';
import { FakeEsp, FakeSerialPort } from '../js/flasher/fake.js';
import { detect, execute } from '../js/flasher/flow.js';
import { SCENARIOS } from '../js/flasher/scenarios.js';

const LAYOUT = [
  { name: 'bootloader', path: 'bootloader.bin', offset: 0x0, size: 21232, fill: 0xe9, modes: ['install'] },
  { name: 'partition-table', path: 'partition-table.bin', offset: 0x8000, size: 3072, fill: 0xaa, modes: ['install'] },
  { name: 'app', path: 'app.bin', offset: 0x10000, size: 65536, fill: 0x55, modes: ['install', 'update'] },
  { name: 'otadata', path: 'ota_data_initial.bin', offset: 0x710000, size: 8192, fill: 0xff, modes: ['install', 'update'] },
];

// Manifests and file bytes for each kind with a web install.
export async function makeFixtures() {
  const bytes = new Map();
  const manifests = {};
  for (const b of BOARDS.filter((x) => x.web)) {
    const files = [];
    for (const f of LAYOUT) {
      const data = new Uint8Array(f.size).fill(f.fill);
      data[1] = b.kind.length;
      const entry = { name: f.name, path: f.path, offset: f.offset, size: f.size, modes: f.modes };
      entry.sha256 = await sha256Hex(data);
      bytes.set(b.kind + '/' + f.path, data);
      files.push(entry);
    }
    manifests[b.kind] = { format: 1, kind: b.kind, chip: b.chip, version: '1.0.79', files };
  }
  const fetchBytes = async (url) => bytes.get(url.split('?')[0]);
  return { manifests, bytes, fetchBytes };
}

function addrs(writes) {
  return writes.map((w) => w.name + '@' + hex(w.address));
}

function same(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}

async function writeWith(sc, fx, kind, mode, log) {
  const port = new FakeSerialPort(sc);
  const esp = new FakeEsp(sc, port, log);
  const board = boardByKind(kind);
  const plan = buildPlan(fx.manifests[kind], mode, board);
  const files = await loadPlanFiles(plan, kind + '/', fx.fetchBytes);
  await execute(esp, plan, files, {});
  return esp;
}

function tableFor(sc, fx, kind) {
  if (sc.table === 'same') {
    return fx.bytes.get(kind + '/partition-table.bin');
  }
  return sc.table === 'other' ? new Uint8Array(3072).fill(0x11) : null;
}

// Runs one scenario. Returns a list of failed checks (empty = PASS).
export async function runScenario(name, fx, log = () => {}) {
  const base = SCENARIOS[name];
  const ex = base.expect;
  const fails = [];
  const check = (ok, what) => {
    if (!ok) {
      fails.push(what);
    }
  };
  const port = new FakeSerialPort(base);
  const kind0 = ex.kind || (ex.candidates[0] || 'lcd349');
  const sc = Object.assign({}, base, { table: tableFor(base, fx, kind0) });
  const res = await detect(port, (p, l) => new FakeEsp(sc, p, l), log);
  const sigs = port.events.filter((e) => e.ev === 'signals');
  const lastSig = port.events.map((e) => e.ev).lastIndexOf('signals');
  const firstWrite = port.events.findIndex((e) => e.ev === 'write');
  const last = sigs[sigs.length - 1];
  check(!!last && !last.dtr && !last.rts, 'DTR and RTS low after the open');
  check(sigs.every((e) => !(e.rts && !e.dtr)), 'no reset state (RTS high, DTR low) at any time');
  check(port.resets === (ex.resets || 0), 'board resets by the probe: ' + (ex.resets || 0) + ', got ' + port.resets);
  check(firstWrite < 0 || lastSig < firstWrite, 'the signals before the first byte');
  check(!port.isOpen, 'the port closes after the console probe');
  const d = res.decision;
  check(!!d.error === !!ex.error, 'error ' + !!ex.error + ', got ' + (d.error || 'none'));
  check(same(d.candidates.map((b) => b.kind), ex.candidates), 'candidates ' + ex.candidates.join(','));
  if (ex.kind) {
    check(d.detected && d.detected.kind === ex.kind, 'the detected kind ' + ex.kind);
  }
  if ('canUpdate' in ex) {
    check(!!d.canUpdate === ex.canUpdate, 'UPDATE possible: ' + ex.canUpdate);
  }
  if (ex.noWebInstall) {
    check(d.candidates.every((b) => !b.web), 'no web install');
  }
  if (ex.install) {
    const esp = await writeWith(sc, fx, kind0, 'install', log);
    check(same(addrs(esp.writes), ex.install), 'INSTALL writes ' + ex.install.join(' '));
    check(esp.eraseAll === true && esp.resets === 1, 'INSTALL erases the flash and resets the board');
  }
  if (ex.update) {
    const esp = await writeWith(sc, fx, kind0, 'update', log);
    check(same(addrs(esp.writes), ex.update), 'UPDATE writes ' + ex.update.join(' '));
    check(esp.eraseAll === false, 'UPDATE does not erase');
    check(esp.reads.some((r) => r.offset === 0x8000), 'UPDATE reads the partition table first');
  }
  if (ex.updateRefused) {
    let refused = false;
    try {
      await writeWith(sc, fx, kind0, 'update', log);
    } catch (e) {
      refused = /partition table/.test(e.message);
    }
    check(refused, 'UPDATE stops on another partition table');
  }
  return fails;
}

// The checks of the plan rules that need no board.
export async function runPlanRules(fx) {
  const fails = [];
  const m = fx.manifests.lcd349;
  const bad = JSON.parse(JSON.stringify(m));
  bad.files[2].offset = 0x8000;
  if (!buildPlan(bad, 'install', boardByKind('lcd349')).errors.length) {
    fails.push('overlapping files must stop the plan');
  }
  if (!buildPlan(m, 'install', boardByKind('cores3')).errors.length) {
    fails.push('a manifest of another kind must stop the plan');
  }
  const plan = buildPlan(m, 'install', boardByKind('lcd349'));
  const wrong = async (url) => {
    const d = await fx.fetchBytes(url);
    const c = d.slice();
    c[100] ^= 1;
    return c;
  };
  let stopped = false;
  try {
    await loadPlanFiles(plan, 'lcd349/', wrong);
  } catch (e) {
    stopped = /SHA-256/.test(e.message);
  }
  if (!stopped) {
    fails.push('a changed file must fail the SHA-256 check');
  }
  const sc = SCENARIOS['rom-s3-16'];
  const esp = new FakeEsp(sc, new FakeSerialPort(sc), () => {});
  const files = await loadPlanFiles(plan, 'lcd349/', fx.fetchBytes);
  const res = await execute(esp, plan, files, { backupBytes: 4096 });
  if (!res.backup || res.backup.length !== 4096 || esp.reads[0].offset !== 0 || !esp.writes) {
    fails.push('the copy of the flash must come from address 0 before the write');
  }
  return fails;
}

export async function runAll(log = () => {}) {
  const fx = await makeFixtures();
  const results = [];
  for (const name of Object.keys(SCENARIOS)) {
    let fails;
    try {
      fails = await runScenario(name, fx, log);
    } catch (e) {
      fails = ['exception: ' + e.message];
    }
    results.push({ name, label: SCENARIOS[name].label, fails });
  }
  results.push({ name: 'plan-rules', label: 'The plan and file checks', fails: await runPlanRules(fx) });
  return results;
}
