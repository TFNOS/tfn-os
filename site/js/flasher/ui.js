// The install page: connect, detect, choose the board and the mode, check the files, write, and report.
// With ?test=<scenario> the page uses the fake port and the fake ROM bootloader (fake.js) and no real port.
import { boardByKind } from '../boards.js';
import { buildPlan, hex, loadPlanFiles } from './core.js';
import { EspSession } from './esp.js';
import { FakeEsp, FakeSerialPort } from './fake.js';
import { detect, execute } from './flow.js';
import { SCENARIOS } from './scenarios.js';

const FW = 'firmware/';
const FILTERS = [{ usbVendorId: 0x303a }, { usbVendorId: 0x1a86 }];
const STAGES = {
  table: 'Reading the partition table of the board',
  backup: 'Saving a copy of the flash',
  erase: 'Erasing the whole flash. This takes up to a minute.',
  write: 'Writing the files',
  reset: 'Starting the board',
  done: 'Done',
};

const $ = (id) => document.getElementById(id);
const state = { port: null, esp: null, det: null, board: null, manifest: null, plan: null, busy: false };
const testName = new URLSearchParams(location.search).get('test');

function log(text) {
  const el = $('log');
  el.textContent += text.replace(/\r/g, '') + (text.endsWith('\n') ? '' : '\n');
  el.scrollTop = el.scrollHeight;
}

function show(id, on) {
  $(id).hidden = !on;
}

function setError(text) {
  $('error').textContent = text || '';
  show('error', !!text);
}

async function fetchBytes(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) {
    throw new Error('The site could not give ' + url.split('?')[0] + ' (HTTP ' + r.status + ').');
  }
  return new Uint8Array(await r.arrayBuffer());
}

async function fetchJson(url) {
  return JSON.parse(new TextDecoder().decode(await fetchBytes(url)));
}

async function testPort() {
  if (!SCENARIOS[testName]) {
    throw new Error('The test scenario "' + testName + '" does not exist.');
  }
  const sc = Object.assign({}, SCENARIOS[testName]);
  const kind = sc.expect.kind || (sc.expect.candidates[0] || 'lcd349');
  if (sc.table === 'same') {
    sc.table = await fetchBytes(FW + kind + '/partition-table.bin');
  } else if (sc.table === 'other') {
    sc.table = new Uint8Array(3072).fill(0x11);
  }
  log('Test mode: scenario "' + testName + '": ' + sc.label + '.');
  return { port: new FakeSerialPort(sc), makeEsp: (p, l) => new FakeEsp(sc, p, l) };
}

async function realPort() {
  const port = await navigator.serial.requestPort({ filters: FILTERS });
  return { port, makeEsp: (p, l) => new EspSession(p, l) };
}

function factRow(dl, name, value) {
  const dt = document.createElement('dt');
  dt.textContent = name;
  const dd = document.createElement('dd');
  dd.textContent = value;
  dl.append(dt, dd);
}

function showFacts(det) {
  const dl = $('facts');
  dl.textContent = '';
  if (det.tfn) {
    factRow(dl, 'TFN OS', det.tfn.version + ' for ' + det.tfn.kind + ' (from ' + det.tfn.slot + ')');
  } else {
    const prot = det.probe.answer && det.probe.answer.protectedMode;
    factRow(dl, 'TFN OS', prot ? 'protected mode: the kind is not known' : 'not found');
  }
  factRow(dl, 'Chip', det.rom.description || det.rom.chip);
  factRow(dl, 'Flash', det.rom.flashText || 'unknown');
  factRow(dl, 'MAC', det.rom.mac || 'unknown');
  if (det.rom.vid !== undefined) {
    factRow(dl, 'USB', hex(det.rom.vid, 4) + ':' + hex(det.rom.pid, 4));
  }
  show('detect-out', true);
}

function miniDevice(board) {
  const fig = document.createElement('figure');
  fig.className = 'dev dev-' + board.kind;
  const body = document.createElement('div');
  body.className = 'dev-body';
  const scr = document.createElement('div');
  scr.className = 'dev-screen';
  const img = document.createElement('img');
  img.src = board.shot;
  img.alt = '';
  scr.appendChild(img);
  body.appendChild(scr);
  fig.appendChild(body);
  return fig;
}

function renderChoices(decision) {
  const box = $('choices');
  box.textContent = '';
  for (const b of decision.candidates) {
    const label = document.createElement('label');
    label.className = 'choice';
    const input = document.createElement('input');
    input.type = 'radio';
    input.name = 'board';
    input.value = b.kind;
    input.checked = decision.candidates.length === 1;
    input.addEventListener('change', () => chooseBoard(b));
    const text = document.createElement('span');
    text.className = 'choice-text';
    text.innerHTML = '<b></b><small></small>';
    text.querySelector('b').textContent = b.name;
    text.querySelector('small').textContent = b.chip + ', ' + b.flashMB + ' MB flash, ' + b.screen;
    label.append(input, miniDevice(b), text);
    box.appendChild(label);
  }
  const one = decision.candidates.length === 1;
  const why = state.det.tfn ? 'The board told its kind. ' : 'Only one board fits this chip. ';
  $('board-msg').textContent = one
    ? why + 'Check that the picture is your board.'
    : 'More than one board has this chip. Choose your board by its picture.';
  show('step-board', true);
  if (one) {
    chooseBoard(decision.candidates[0]);
  }
}

async function onConnect() {
  if (state.busy) {
    return;
  }
  state.busy = true;
  setError('');
  ['detect-out', 'step-board', 'step-mode', 'step-progress'].forEach((id) => show(id, false));
  try {
    if (state.esp) {
      await state.esp.close();
    }
    const p = testName ? await testPort() : await realPort();
    state.port = p.port;
    $('btn-connect').textContent = 'Connecting...';
    state.det = await detect(p.port, p.makeEsp, log);
    state.esp = state.det.esp;
    showFacts(state.det);
    if (state.det.decision.error) {
      setError(state.det.decision.error);
      return;
    }
    renderChoices(state.det.decision);
  } catch (e) {
    connectFailed(e);
  } finally {
    state.busy = false;
    $('btn-connect').textContent = 'Connect again';
  }
}

function connectFailed(e) {
  if (e && e.name === 'NotFoundError') {
    setError('No port was chosen. Connect the board, click Connect, and choose its port.');
    return;
  }
  log('Fault: ' + (e && e.message));
  setError((e && e.message ? e.message + ' ' : '') +
    'Check the cable, close other programs that use the port, and try again. ' +
    'If the board still does not answer, hold its BOOT key while you connect it.');
}

async function chooseBoard(board) {
  state.board = board;
  show('step-mode', false);
  setError('');
  if (!board.web) {
    setError('TFN OS on the ' + board.short + ' starts from a boot menu that the web install cannot write yet. ' +
      'The install guide for this board is coming.');
    return;
  }
  try {
    const idx = await fetchJson(FW + 'index.json');
    const entry = idx.kinds && idx.kinds[board.kind];
    if (!entry) {
      throw new Error('This site has no release for the ' + board.short + ' now.');
    }
    state.manifest = await fetchJson(FW + entry.manifest);
  } catch (e) {
    setError(e.message);
    return;
  }
  renderModes();
}

function canUpdate() {
  const det = state.det;
  return !!(det && det.decision.canUpdate && det.tfn && det.tfn.kind === state.board.kind);
}

function renderModes() {
  const up = canUpdate();
  $('mode-update').disabled = !up;
  $('mode-update-note').textContent = up
    ? 'The board runs TFN OS ' + state.det.tfn.version + '. UPDATE writes ' + state.manifest.version +
      ' and keeps the apps, the settings and the WiFi.'
    : 'Only for a board that runs TFN OS for this board kind.';
  $('mode-' + (up ? 'update' : 'install')).checked = true;
  $('fw-version').textContent = state.manifest.version;
  show('step-mode', true);
  renderPlan();
}

function selectedMode() {
  return $('mode-update').checked ? 'update' : 'install';
}

function planRow(f) {
  const tr = document.createElement('tr');
  tr.id = 'row-' + f.name;
  for (const v of [f.name, hex(f.offset), f.size.toLocaleString('en') + ' B', f.sha256.slice(0, 16) + '...', 'not checked']) {
    const td = document.createElement('td');
    td.textContent = v;
    tr.appendChild(td);
  }
  return tr;
}

function renderPlan() {
  const mode = selectedMode();
  state.plan = buildPlan(state.manifest, mode, state.board);
  const body = $('plan-body');
  body.textContent = '';
  state.plan.writes.forEach((f) => body.appendChild(planRow(f)));
  const install = mode === 'install';
  show('erase-warn', install);
  show('backup-opt', install);
  show('confirm-opt', install);
  $('btn-write').textContent = (install ? 'Install ' : 'Update to ') + state.manifest.version;
  $('btn-write').classList.toggle('btn-danger', install);
  if (state.plan.errors.length) {
    setError('The release files do not pass the checks: ' + state.plan.errors.join('; ') + '.');
  }
  updateWriteButton();
}

function updateWriteButton() {
  const ok = state.plan && !state.plan.errors.length && !state.busy;
  const confirmed = selectedMode() === 'update' || $('opt-confirm').checked;
  $('btn-write').disabled = !(ok && confirmed);
}

function setStage(stage) {
  $('prog-text').textContent = STAGES[stage] || stage;
  if (stage === 'erase' || stage === 'table' || stage === 'reset') {
    $('prog').removeAttribute('value');
  }
}

function setProgress(done, total) {
  $('prog').value = total ? done / total : 0;
  $('prog-pct').textContent = total ? Math.round((100 * done) / total) + ' %' : '';
}

function offerBackup(bytes) {
  const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
  const a = $('backup-link');
  a.href = url;
  a.download = 'flash-' + (state.det.rom.mac || 'board').replace(/:/g, '') + '.bin';
  show('backup-out', true);
  a.click();
}

async function onWrite() {
  if (state.busy || !state.plan) {
    return;
  }
  state.busy = true;
  updateWriteButton();
  setError('');
  show('step-progress', true);
  const verb = state.plan.mode === 'install' ? 'Installing TFN OS ' : 'Updating to TFN OS ';
  $('prog-title').textContent = verb + state.plan.version;
  try {
    setStage('Checking the SHA-256 of each file');
    const files = await loadPlanFiles(state.plan, FW + state.board.kind + '/', fetchBytes, (f) => {
      document.querySelector('#row-' + f.name + ' td:last-child').textContent = 'SHA-256 OK';
    });
    const backupBytes = state.plan.mode === 'install' && $('opt-backup').checked ? state.det.rom.flashMB * 1024 * 1024 : 0;
    const res = await execute(state.esp, state.plan, files, {
      backupBytes, onStage: setStage, onProgress: setProgress, onBackup: setProgress,
    });
    if (res.backup) {
      offerBackup(res.backup);
    }
    setProgress(1, 1);
    $('prog-text').textContent = 'Done. The board starts TFN OS ' + state.plan.version + '. ' +
      (state.plan.mode === 'install' ? 'On the board, open SETUP > WIFI to join your network.' : 'Your apps and settings stay.');
    log('Done: ' + state.plan.mode + ' ' + state.plan.version + ' for ' + state.board.kind + '.');
    await state.esp.close();
    state.esp = null;
    state.plan = null;
  } catch (e) {
    log('Fault: ' + e.message);
    setError(e.message + ' Nothing more is written. Click Connect again to start over.');
    $('prog-text').textContent = 'Stopped.';
  } finally {
    state.busy = false;
    updateWriteButton();
  }
}

function initTestMode() {
  if (!testName) {
    return;
  }
  const sel = $('scenario');
  for (const [name, sc] of Object.entries(SCENARIOS)) {
    const o = document.createElement('option');
    o.value = name;
    o.textContent = sc.label;
    o.selected = name === testName;
    sel.appendChild(o);
  }
  sel.addEventListener('change', () => {
    location.search = '?test=' + sel.value;
  });
  show('testbar', true);
}

function init() {
  initTestMode();
  if (!('serial' in navigator) && !testName) {
    show('unsupported', true);
    $('btn-connect').disabled = true;
    return;
  }
  $('btn-connect').addEventListener('click', onConnect);
  $('btn-write').addEventListener('click', onWrite);
  $('mode-update').addEventListener('change', renderPlan);
  $('mode-install').addEventListener('change', renderPlan);
  $('opt-confirm').addEventListener('change', updateWriteButton);
}

init();
