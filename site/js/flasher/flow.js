// The steps of the flasher in their order. The page (ui.js) and the tests use the same steps; only the port and the
// ROM bootloader session differ (real or fake).
import { decide, tableMatches } from './core.js';
import { probeConsole } from './probe.js';

// Step 1 and 2: the console of a running TFN OS (no reset), then the ROM bootloader (a reset into the download mode).
export async function detect(port, makeEsp, log) {
  let probe = { text: '', answer: null };
  try {
    probe = await probeConsole(port, log);
  } catch (e) {
    log('Console: no answer (' + e.message + ').');
  }
  const tfn = probe.answer && probe.answer.kind ? probe.answer : null;
  if (tfn) {
    log('Console: TFN OS ' + tfn.version + ' for "' + tfn.kind + '" runs from ' + tfn.slot + '.');
  } else if (probe.answer && probe.answer.protectedMode) {
    log('Console: the board is in the protected mode. It does not tell its kind.');
  } else {
    log('Console: TFN OS did not answer.');
  }
  const esp = makeEsp(port, log);
  const rom = await esp.connect();
  log('Chip: ' + rom.chip + ', flash ' + (rom.flashText || 'unknown') + ', MAC ' + rom.mac + '.');
  return { probe, tfn, rom, decision: decide(tfn, rom), esp };
}

// Step 3: the write. UPDATE first reads the partition table of the board and stops when it is not the release table.
// backupBytes > 0: a copy of the flash from 0 comes back as result.backup before the first write.
export async function execute(esp, plan, files, opts) {
  const stage = opts.onStage || (() => {});
  const result = { backup: null };
  if (plan.errors.length) {
    throw new Error(plan.errors.join('; '));
  }
  if (plan.mode === 'update') {
    stage('table');
    const table = await esp.readFlash(plan.table.offset, plan.table.size);
    if (!(await tableMatches(plan, table))) {
      throw new Error('The partition table of the board is not the table of this release. Use INSTALL.');
    }
  }
  if (opts.backupBytes) {
    stage('backup');
    result.backup = await esp.readFlash(0, opts.backupBytes, opts.onBackup);
  }
  stage(plan.eraseAll ? 'erase' : 'write');
  const total = files.reduce((s, f) => s + f.data.length, 0);
  const before = files.map((f, i) => files.slice(0, i).reduce((s, g) => s + g.data.length, 0));
  // (esptool-js tells the sent bytes of the compressed file: the part of each file, weighted by its real size)
  await esp.write(files, plan.eraseAll, (i, done, fileTotal) => {
    stage('write');
    if (opts.onProgress) {
      const part = fileTotal ? Math.min(1, done / fileTotal) : 1;
      opts.onProgress(before[i] + part * files[i].data.length, total);
    }
  });
  stage('reset');
  await esp.reset();
  stage('done');
  return result;
}
