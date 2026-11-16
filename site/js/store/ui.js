// The store page: a gallery of the apps of the static store, and the install over USB. The page reads the index of
// the store, checks that a key of STORE_KEYS signed it, and shows the apps of one board kind. With a board on USB
// (Web Serial), it reads the kind, the version and the apps of the board, and installs an app with one click
// (usb.js). With no Web Serial, the page gives the signed zip file of an app.
import { BOARDS, boardByKind } from '../boards.js';
import { stat } from '../stats.js';
import {
  appsFor, checkIndex, decodeIcon, keyId, kindsOf, loadChecked, sizeText, trustedKey, verCmp, verifySig,
} from './core.js';
import { BoardLink } from './usb.js';

const STORE = 'store/';
const MEDIA = 'media/apps/';
// the static store of the TFNOS org (main/tfn_defaults.h); a board of TFN OS 1.0.0 has the old address
const BOARD_STORE = 'https://tfnos.github.io/tfn-os/store/';
const OLD_STORE = 'omeriko9.github.io';
const FILTERS = [{ usbVendorId: 0x303a }, { usbVendorId: 0x1a86 }];
const TINTS = { GAMES: '#3b2f9a', UTIL: '#0d5d58', TOOLS: '#0d5d58' };

const $ = (id) => document.getElementById(id);
const state = { index: null, kind: '', group: 'all', media: {}, link: null, board: null, icons: new Map(), dlg: null };

// ---------- the files ----------

async function fetchBytes(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) {
    throw new Error('The site could not give ' + url.split('?')[0] + ' (HTTP ' + r.status + ').');
  }
  return new Uint8Array(await r.arrayBuffer());
}

async function fetchJson(url) {
  try {
    const r = await fetch(url, { cache: 'no-cache' });
    return r.ok ? await r.json() : {};
  } catch (e) {
    return {};
  }
}

function setError(text) {
  $('error').textContent = text || '';
  $('error').hidden = !text;
}

function saveFile(name, data) {
  const url = URL.createObjectURL(new Blob([data], { type: 'application/zip' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}

// ---------- the animated icons (icon.bin of each app) ----------

const iconCache = new Map();

function iconOf(entry) {
  const want = entry.files['icon.bin'];
  if (!want) {
    return Promise.resolve(null);
  }
  const url = STORE + entry.path + 'icon.bin';
  if (!iconCache.has(url)) {
    iconCache.set(url, loadChecked(url, want, fetchBytes).then(decodeIcon).catch(() => null));
  }
  return iconCache.get(url);
}

function paintIcon(canvas, ic, f) {
  if (canvas.width !== ic.size) {
    canvas.width = ic.size;
    canvas.height = ic.size;
  }
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(ic.size, ic.size);
  const idx = ic.frames[f % ic.frames.length];
  for (let i = 0; i < idx.length; i++) {
    const c = 4 * idx[i] < ic.rgba.length ? 4 * idx[i] : 0;
    img.data[4 * i] = ic.rgba[c];
    img.data[4 * i + 1] = ic.rgba[c + 1];
    img.data[4 * i + 2] = ic.rgba[c + 2];
    img.data[4 * i + 3] = ic.rgba[c + 3];
  }
  ctx.putImageData(img, 0, 0);
}

const seen = 'IntersectionObserver' in window ? new IntersectionObserver((list) => {
  for (const e of list) {
    const it = state.icons.get(e.target);
    if (it) {
      it.visible = e.isIntersecting;
    }
  }
}) : null;
const still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

function addIcon(canvas, entry) {
  iconOf(entry).then((ic) => {
    if (!ic || !canvas.isConnected) {
      return;
    }
    state.icons.set(canvas, { ic, f: 0, t: 0, visible: !seen });
    if (seen) {
      seen.observe(canvas);
    }
    paintIcon(canvas, ic, 0);
  });
}

function tick(now) {
  for (const [canvas, it] of state.icons) {
    if (!canvas.isConnected) {
      state.icons.delete(canvas);
      continue;
    }
    if (it.visible && !still && it.ic.frames.length > 1 && now - it.t >= it.ic.ms) {
      it.t = now;
      it.f = (it.f + 1) % it.ic.frames.length;
      paintIcon(canvas, it.ic, it.f);
    }
  }
  requestAnimationFrame(tick);
}

// ---------- one app ----------

function groupOf(app) {
  return app.group === 'GAMES' ? 'games' : 'tools';
}

function firstSentences(text, max) {
  const parts = (text || '').split(/(?<=\.)\s+/);
  let out = '';
  for (const p of parts) {
    if (out && (out + ' ' + p).length > max) {
      break;
    }
    out = out ? out + ' ' + p : p;
  }
  return out;
}

function mediaOf(app) {
  return state.media[app.id] || null;
}

// the picture part of a card or of the window: the preview of the app, else its icon
function shotEl(app, entry, big) {
  const m = mediaOf(app);
  const el = document.createElement(big ? 'div' : 'button');
  el.className = 'shot';
  if (!big) {
    el.type = 'button';
    el.setAttribute('aria-label', 'More about ' + app.name);
  }
  if (m && (m.anim || m.poster)) {
    const img = document.createElement('img');
    img.src = MEDIA + (still && m.poster ? m.poster : m.anim || m.poster);
    img.alt = big ? app.name + ' on the board' : '';
    img.loading = 'lazy';
    img.decoding = 'async';
    img.width = m.w || 640;
    img.height = m.h || 172;
    el.appendChild(img);
  } else {
    el.classList.add('icon-only');
    el.style.setProperty('--tint', TINTS[app.group] || '#2d2f6a');
    const c = document.createElement('canvas');
    c.width = 64;
    c.height = 64;
    c.setAttribute('aria-hidden', 'true');
    el.appendChild(c);
    addIcon(c, entry);
  }
  return el;
}

// what the main button of an app does now: { label, act: 'install' | 'download' | 'connect' | 'none', done }
function action(app, entry) {
  const b = state.board;
  if (b) {
    const have = b.apps[app.id];
    // ("apps" gives the ABI form of the board, not its rev: a board with an old rev refuses the app at the install,
    // and the page shows the reason of the board)
    if (b.abi >= 0 && entry.abi > b.abi) {
      return { label: 'Needs a newer TFN OS', act: 'none' };
    }
    if (have && verCmp(have.ver, app.ver) >= 0) {
      return { label: 'Installed', act: 'install', done: true };
    }
    return { label: have ? 'Update' : 'Install', act: 'install' };
  }
  if ('serial' in navigator) {
    return { label: 'Install', act: 'connect' };
  }
  return { label: 'Download', act: 'download' };
}

function facts(app, entry) {
  const b = boardByKind(state.kind);
  const rows = [['Version', app.ver], ['Size', sizeText(entry.bytes)], ['Board', b ? b.short : state.kind]];
  if (entry.ram) {
    rows.push(['Memory', sizeText(entry.ram) + ' while it runs']);
  }
  if (entry.perms && entry.perms.length) {
    rows.push(['It can use', entry.perms.join(', ')]);
  }
  const pack = packOf(app);
  if (pack) {
    rows.push(['Data on the SD card', sizeText(pack.bytes) + ' (the page copies it with the app)']);
  }
  const have = state.board && state.board.apps[app.id];
  if (have) {
    rows.push(['On your board', have.ver + (have.place === 'card' ? ' (SD card)' : '')]);
  }
  return rows;
}

// the data pack of an app, or null. The index (sdk/tools/store_index.py make --packs): "packs": [{ name, dir, path,
// size, files: [{ path, size, sha256 }] }] and the field "pack" of the app. A file goes to /sdcard/<dir>/<path>; the
// store has it at <store>/<pack path><path>.
function packOf(app) {
  const packs = state.index && Array.isArray(state.index.packs) ? state.index.packs : [];
  const p = app.pack ? packs.find((x) => x && x.name === app.pack) : null;
  if (!p || !Array.isArray(p.files) || !/^[a-z0-9_]+$/.test(p.dir || '') || !/^packs\/[a-z0-9_]+\/$/.test(p.path || '')) {
    return null;
  }
  const files = p.files.filter((f) => f && /^[A-Za-z0-9_.-]+(\/[A-Za-z0-9_.-]+)*$/.test(f.path || '') &&
    !f.path.split('/').some((x) => x === '..' || x === '.'));
  return { name: p.name, dir: p.dir, path: p.path, files, bytes: files.reduce((s, f) => s + f.size, 0) };
}

// ---------- the install ----------

async function connect() {
  setError('');
  let port;
  try {
    port = await navigator.serial.requestPort({ filters: FILTERS });
  } catch (e) {
    return false;
  }
  const link = new BoardLink(port);
  try {
    await link.open();
  } catch (e) {
    setError('The page could not open the USB port. Close other programs that use the board (a serial monitor, ' +
      'another tab), then try again.');
    return false;
  }
  const b = await link.hello().catch(() => null);
  if (!b || !b.kind) {
    await link.close();
    setError(b && b.protectedMode ? 'The board is in the protected mode: its console needs the admin password.'
      : 'No TFN OS answered on this port. Is TFN OS on the board? Use the install page to put it on the board.');
    stat('store-connect', { ok: 0 });
    return false;
  }
  state.link = link;
  state.board = b;
  stat('store-connect', { ok: 1, kind: b.kind, ver: b.version, card: b.card ? 1 : 0, napps: b.count });
  let note = '';
  if (b.store && b.store.url.includes(OLD_STORE)) {
    const r = await link.fixStore(BOARD_STORE).catch(() => null);
    note = r && r.url === BOARD_STORE ? 'The page also set the store address of the board to the new address of ' +
      'the TFN OS store (TFN OS 1.0.0 has an old address).' : '';
  }
  navigator.serial.addEventListener('disconnect', onLost);
  showBoard(note);
  if ([...$('kind').options].some((o) => o.value === b.kind)) {
    $('kind').value = b.kind;
    state.kind = b.kind;
  }
  render();
  return true;
}

function onLost(e) {
  if (state.link && e.target === state.link.port) {
    state.link = null;
    state.board = null;
    showBoard('');
    render();
    setError('The board is disconnected.');
  }
}

async function disconnect() {
  const l = state.link;
  state.link = null;
  state.board = null;
  if (l) {
    await l.close();
  }
  showBoard('');
  render();
}

function showBoard(note) {
  const b = state.board;
  $('link-idle').hidden = !!b;
  $('link-on').hidden = !b;
  $('kind-pick').hidden = !!b;
  if (!b) {
    return;
  }
  const board = boardByKind(b.kind);
  $('lk-name').textContent = board ? board.short : b.kind;
  const n = Object.keys(b.apps).length;
  $('lk-facts').textContent = 'TFN OS ' + b.version + ' · ' + n + (n === 1 ? ' app' : ' apps') + ' · ' +
    (b.card ? 'SD card in' : 'no SD card');
  $('lk-note').textContent = note;
  $('lk-note').hidden = !note;
}

// Install one app on the connected board. ui = { bar, msg, btn } of the card or of the window.
async function install(app, entry, ui) {
  if (!state.link && !(await connect())) {
    return;
  }
  const b = state.board;
  const a = action(app, entry);
  if (a.act === 'none') {
    ui.msg.textContent = 'This app needs a newer TFN OS. Update it on the install page (your apps stay).';
    return;
  }
  const pack = packOf(app);
  if (pack && !b.card) {
    ui.msg.textContent = 'This app needs an SD card in the board (' + sizeText(pack.bytes) + ' of data).';
    ui.msg.classList.add('bad');
    return;
  }
  const t0 = Date.now();
  ui.btn.disabled = true;
  ui.bar.hidden = false;
  ui.msg.classList.remove('bad');
  const span = ui.bar.firstElementChild;
  let stepT = 0;
  let stepWhat = '';
  const show = (done, total, what) => {
    if (what !== stepWhat) {
      stepWhat = what;
      stepT = Date.now();
    }
    span.style.width = (total ? Math.round((100 * done) / total) : 0) + '%';
    const sec = (Date.now() - stepT) / 1000;
    const left = done > 20000 && sec > 3 ? Math.round(((total - done) * sec) / done) : -1;
    ui.msg.textContent = what + ' ' + sizeText(done) + ' of ' + sizeText(total) +
      (left > 60 ? ', about ' + Math.ceil(left / 60) + ' min left' : left > 5 ? ', about ' + left + ' s left' : '');
  };
  try {
    ui.msg.textContent = 'The page gets the files and checks them.';
    const files = {};
    for (const [name, want] of Object.entries(entry.files)) {
      files[name] = await loadChecked(STORE + entry.path + name, want, fetchBytes);
    }
    const have = b.apps[app.id];
    const where = await state.link.install(app.id, files, b.card, have, (d, t) => show(d, t, 'Sending:'));
    if (pack) {
      const list = [];
      let got = 0;
      for (const f of pack.files) {
        const data = await loadChecked(STORE + pack.path + f.path, f, fetchBytes);
        list.push({ path: '/' + pack.dir + '/' + f.path, data });
        got += data.length;
        show(got, pack.bytes, 'The page gets the data of the app:');
      }
      await state.link.pack(list, (d, t) => show(d, t, 'The data of the app:'));
    }
    // (the board adds the app to its list a short time after the commit)
    for (let i = 0; i < 5; i++) {
      await new Promise((r) => setTimeout(r, 400));
      const now = await state.link.apps();
      if (now && now.apps[app.id]) {
        b.apps = now.apps;
        break;
      }
    }
    if (!b.apps[app.id] || verCmp(b.apps[app.id].ver, app.ver) < 0) {
      b.apps[app.id] = { ver: app.ver, place: where.card ? 'card' : 'board' };
    }
    span.style.width = '100%';
    ui.msg.textContent = 'Installed' + (where.card ? ' on the SD card' : '') + '. Open it from the menu of the board.';
    stat('store-install', { id: app.id, kind: b.kind, ver: app.ver, ok: 1, sec: Math.round((Date.now() - t0) / 1000) });
    showBoard($('lk-note').textContent);
    render();
  } catch (e) {
    let text = e.message;
    if (/BAD_REQUEST/.test(text) && !b.card) {
      text = 'TFN OS ' + b.version + ' can take an app over USB only on an SD card. Put a card in the board, or ' +
        'update TFN OS on the install page (your apps stay).';
    }
    ui.msg.textContent = text;
    ui.msg.classList.add('bad');
    stat('store-install', { id: app.id, kind: b.kind, ver: app.ver, ok: 0, err: e.message });
  } finally {
    ui.btn.disabled = false;
    setTimeout(() => {
      ui.bar.hidden = true;
    }, 1500);
  }
}

async function download(app, entry, ui) {
  ui.btn.disabled = true;
  try {
    const data = await loadChecked(STORE + entry.zip.path, entry.zip, fetchBytes);
    saveFile(entry.zip.path.split('/').pop(), data);
    ui.msg.textContent = 'The file is checked and saved. "Other ways to install", below the apps, tells how to use it.';
    stat('store-download', { id: app.id, kind: state.kind });
  } catch (e) {
    ui.msg.textContent = e.message;
    ui.msg.classList.add('bad');
  } finally {
    ui.btn.disabled = false;
  }
}

function doAction(app, entry, ui) {
  const a = action(app, entry);
  if (a.act === 'download') {
    return download(app, entry, ui);
  }
  if (a.act === 'install' || a.act === 'connect') {
    return install(app, entry, ui);
  }
  return null;
}

function setButton(btn, app, entry) {
  const a = action(app, entry);
  btn.textContent = a.label;
  btn.className = 'btn' + (a.done ? ' done' : a.act === 'none' ? '' : ' btn-primary');
  btn.disabled = a.act === 'none';
  btn.title = a.done ? 'Install it again' : '';
}

// ---------- the gallery ----------

function card(app, entry, wide) {
  const li = document.createElement('li');
  li.className = 'card' + (wide ? ' wide' : '');
  const shot = shotEl(app, entry, false);
  shot.addEventListener('click', () => openDlg(app, entry));
  const body = document.createElement('div');
  body.className = 'card-body';
  const head = document.createElement('div');
  head.className = 'card-head';
  const ico = document.createElement('canvas');
  ico.className = 'ico';
  ico.width = 40;
  ico.height = 40;
  ico.setAttribute('aria-hidden', 'true');
  addIcon(ico, entry);
  const nm = document.createElement('div');
  nm.className = 'card-name';
  const h = document.createElement('h3');
  const hb = document.createElement('button');
  hb.type = 'button';
  hb.textContent = app.name;
  hb.addEventListener('click', () => openDlg(app, entry));
  h.appendChild(hb);
  const tag = document.createElement('span');
  tag.className = 'tag ' + groupOf(app);
  tag.textContent = groupOf(app) === 'games' ? 'Game' : 'Tool';
  nm.append(h, tag);
  head.append(ico, nm);
  const desc = document.createElement('p');
  desc.className = 'card-desc';
  desc.textContent = firstSentences(app.desc, wide ? 260 : 150);
  const foot = document.createElement('div');
  foot.className = 'card-foot';
  const meta = document.createElement('span');
  meta.className = 'card-meta';
  const pack = packOf(app);
  meta.textContent = sizeText(entry.bytes + (pack ? pack.bytes : 0)) + ' · v' + app.ver;
  const btn = document.createElement('button');
  btn.type = 'button';
  setButton(btn, app, entry);
  foot.append(meta, btn);
  const msg = document.createElement('p');
  msg.className = 'card-msg';
  msg.setAttribute('role', 'status');
  const bar = document.createElement('div');
  bar.className = 'bar';
  bar.hidden = true;
  bar.appendChild(document.createElement('span'));
  btn.addEventListener('click', () => doAction(app, entry, { btn, msg, bar }));
  body.append(head, desc, foot, msg);
  li.append(shot, body, bar);
  return li;
}

function rows() {
  const list = appsFor(state.index, state.kind).filter((r) => state.group === 'all' || groupOf(r.app) === state.group);
  // the games with a preview first, then the other games, then the tools
  const rank = (r) => (groupOf(r.app) === 'games' ? 0 : 2) + (mediaOf(r.app) ? 0 : 1);
  return list.sort((x, y) => rank(x) - rank(y) || x.app.name.localeCompare(y.app.name));
}

function render() {
  const list = $('apps');
  list.textContent = '';
  const all = rows();
  let wide = 0;
  for (const r of all) {
    const big = state.group !== 'tools' && groupOf(r.app) === 'games' && !!mediaOf(r.app) && wide < 2;
    wide += big ? 1 : 0;
    list.appendChild(card(r.app, r.entry, big));
  }
  if (!all.length) {
    const li = document.createElement('li');
    li.textContent = 'No apps of this kind for this board yet.';
    list.appendChild(li);
  }
}

// ---------- the window of one app ----------

function openDlg(app, entry) {
  const d = $('dlg');
  state.dlg = { app, entry };
  const media = $('dlg-media');
  media.textContent = '';
  media.appendChild(shotEl(app, entry, true));
  const ico = $('dlg-ico');
  addIcon(ico, entry);
  $('dlg-name').textContent = app.name;
  $('dlg-group').textContent = groupOf(app) === 'games' ? 'Game' : 'Tool';
  $('dlg-group').className = 'tag ' + groupOf(app);
  $('dlg-desc').textContent = app.desc || '';
  $('dlg-he').textContent = app.desc_he || '';
  $('dlg-he-box').hidden = !app.desc_he;
  const dl = $('dlg-facts');
  dl.textContent = '';
  for (const [k, v] of facts(app, entry)) {
    const dt = document.createElement('dt');
    dt.textContent = k;
    const dd = document.createElement('dd');
    dd.textContent = v;
    dl.append(dt, dd);
  }
  const extra = $('dlg-extra');
  extra.textContent = '';
  const m = mediaOf(app);
  for (const x of (m && m.extra) || []) {
    const img = document.createElement('img');
    img.src = MEDIA + (x.file || x);
    img.alt = app.name + (x.what ? ', ' + x.what : '');
    img.loading = 'lazy';
    extra.appendChild(img);
  }
  setButton($('dlg-install'), app, entry);
  $('dlg-download').textContent = 'Download the signed file (' + sizeText(entry.zip.size) + ')';
  $('dlg-msg').textContent = '';
  $('dlg-msg').classList.remove('bad');
  $('dlg-bar').hidden = true;
  if (typeof d.showModal === 'function') {
    d.showModal();
  } else {
    d.setAttribute('open', '');
  }
  stat('store-view', { id: app.id });
}

function dlgUi() {
  return { btn: $('dlg-install'), msg: $('dlg-msg'), bar: $('dlg-bar') };
}

$('dlg-x').addEventListener('click', () => $('dlg').close());
$('dlg').addEventListener('click', (e) => {
  if (e.target === $('dlg')) {
    $('dlg').close();
  }
});
$('dlg-install').addEventListener('click', async () => {
  const s = state.dlg;
  if (s) {
    await doAction(s.app, s.entry, dlgUi());
    setButton($('dlg-install'), s.app, s.entry);
  }
});
$('dlg-download').addEventListener('click', () => {
  const s = state.dlg;
  if (s) {
    download(s.app, s.entry, { btn: $('dlg-download'), msg: $('dlg-msg'), bar: $('dlg-bar') });
  }
});

// ---------- the start ----------

function renderKinds() {
  const sel = $('kind');
  sel.textContent = '';
  for (const k of kindsOf(state.index, BOARDS)) {
    const o = document.createElement('option');
    const b = boardByKind(k);
    o.value = k;
    o.textContent = b ? b.short : k;
    sel.appendChild(o);
  }
  sel.addEventListener('change', () => {
    state.kind = sel.value;
    render();
  });
  state.kind = sel.value;
}

for (const chip of document.querySelectorAll('.chip')) {
  chip.addEventListener('click', () => {
    state.group = chip.dataset.group;
    for (const c of document.querySelectorAll('.chip')) {
      c.setAttribute('aria-pressed', String(c === chip));
    }
    render();
  });
}

async function init() {
  if (!('serial' in navigator)) {
    $('connect').hidden = true;
    $('no-usb').hidden = false;
  }
  $('connect').addEventListener('click', () => connect());
  $('disconnect').addEventListener('click', () => disconnect());
  requestAnimationFrame(tick);
  try {
    const [data, sig, media] = await Promise.all([fetchBytes(STORE + 'index.json'), fetchBytes(STORE + 'index.sig'),
      fetchJson(MEDIA + 'media.json')]);
    const index = JSON.parse(new TextDecoder().decode(data));
    const faults = checkIndex(index);
    if (faults.length) {
      throw new Error('The store index is not good: ' + faults.slice(0, 3).join('; ') + '.');
    }
    if (!trustedKey(index.pubkey)) {
      throw new Error('The store index has a key that this page does not trust. Do not use these files.');
    }
    if (!(await verifySig(data, sig, index.pubkey))) {
      throw new Error('The signature of the store index is not good. Do not use these files.');
    }
    state.index = index;
    state.media = media || {};
    $('key-id').textContent = (await keyId(index.pubkey)).toUpperCase();
    $('release').textContent = index.release || '-';
    $('store-facts').hidden = false;
    renderKinds();
    render();
  } catch (e) {
    setError(e.message + ' This site has no store now.');
  }
}

init();
