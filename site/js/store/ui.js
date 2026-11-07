// The store page: read the index of the static store, check its signature, show the apps of the chosen board kind,
// and give the files of an app after a check of their size and SHA-256.
import { BOARDS, boardByKind } from '../boards.js';
import { appsFor, checkIndex, keyId, kindsOf, loadChecked, sizeText, verifySig } from './core.js';

const STORE = 'store/';
const $ = (id) => document.getElementById(id);
const state = { index: null, kind: '' };

async function fetchBytes(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) {
    throw new Error('The site could not give ' + url.split('?')[0] + ' (HTTP ' + r.status + ').');
  }
  return new Uint8Array(await r.arrayBuffer());
}

function setError(text) {
  $('error').textContent = text || '';
  $('error').hidden = !text;
}

function saveFile(name, data) {
  const url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}

async function download(app, entry, btn) {
  btn.disabled = true;
  setError('');
  try {
    const data = await loadChecked(STORE + entry.zip.path, entry.zip, fetchBytes);
    saveFile(entry.zip.path.split('/').pop(), data);
    btn.textContent = 'Checked and saved';
  } catch (e) {
    setError(e.message);
    btn.disabled = false;
  }
}

function appCard(app, entry) {
  const li = document.createElement('li');
  li.className = 'app-card';
  const h = document.createElement('h3');
  h.textContent = (app.emoji ? app.emoji + ' ' : '') + app.name;
  const meta = document.createElement('p');
  meta.className = 'app-meta';
  meta.textContent = 'Version ' + app.ver + ' · ' + sizeText(entry.bytes) + ' · ' + app.group.toLowerCase() +
    (entry.perms.length ? ' · it can use: ' + entry.perms.join(', ') : '');
  const desc = document.createElement('p');
  desc.textContent = app.desc || '';
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'btn btn-primary';
  btn.textContent = 'Download ' + entry.zip.path.split('/').pop() + ' (' + sizeText(entry.zip.size) + ')';
  btn.addEventListener('click', () => download(app, entry, btn));
  li.append(h, meta, desc, btn);
  return li;
}

function renderApps() {
  const list = $('apps');
  list.textContent = '';
  const rows = appsFor(state.index, state.kind);
  for (const r of rows) {
    list.appendChild(appCard(r.app, r.entry));
  }
  const b = boardByKind(state.kind);
  $('apps-title').textContent = rows.length + ' apps for the ' + (b ? b.short : state.kind);
  $('step-apps').hidden = false;
  $('step-install').hidden = false;
}

function renderKinds() {
  const sel = $('kind');
  sel.textContent = '';
  for (const k of kindsOf(state.index, BOARDS)) {
    const o = document.createElement('option');
    const b = boardByKind(k);
    o.value = k;
    o.textContent = b ? b.name : k;
    sel.appendChild(o);
  }
  sel.addEventListener('change', () => {
    state.kind = sel.value;
    renderApps();
  });
  state.kind = sel.value;
  renderApps();
}

async function init() {
  try {
    const data = await fetchBytes(STORE + 'index.json');
    const sig = await fetchBytes(STORE + 'index.sig');
    const index = JSON.parse(new TextDecoder().decode(data));
    const faults = checkIndex(index);
    if (faults.length) {
      throw new Error('The store index is not good: ' + faults.slice(0, 3).join('; ') + '.');
    }
    if (!(await verifySig(data, sig, index.pubkey))) {
      throw new Error('The signature of the store index is not good. Do not use these files.');
    }
    state.index = index;
    $('key-id').textContent = (await keyId(index.pubkey)).toUpperCase();
    $('release').textContent = index.release || '-';
    $('store-facts').hidden = false;
    renderKinds();
  } catch (e) {
    setError(e.message + ' This site has no store now.');
  }
}

init();
