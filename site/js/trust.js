// The trust map (trust.html): the sequence diagrams of the setups, drawn as SVG from the data below. A setup = parts
// (the columns) and steps from the top down:
//   {phase}                                a band with a name
//   {from, to, text, ret}                  an arrow (ret: an answer, dashed)
//   {at, sign, title, text}                a box on the line of 'at': a key signs (sign = off | pool | dev)
//   {at, check: [..], fail: [..], title}   the checks: green lines must pass, red lines = refused
//   {at, note, title, kind}                a plain box (kind 'hand' = a person acts)
// The page has the CSP style-src 'self': no style attributes; the colours are classes of css/trust.css.

const SETUPS = [
  {
    id: 'public', tab: 'Public user', color: 'off',
    title: 'A public user: the release of TFNOS/tfn-os and the store on tfnos.xyz',
    intro: 'Someone with a board, Chrome or Edge and a USB cable: no keys, no tools. GitHub signs everything with the official keys, and the board trusts only those keys.',
    parts: [
      { id: 'gh', name: 'GitHub', sub: 'TFNOS/tfn-os: Actions', c: 'off' },
      { id: 'site', name: 'tfnos.xyz', sub: 'install page + store', c: 'off' },
      { id: 'pc', name: 'The user’s PC', sub: 'Chrome or Edge', c: 'dev' },
      { id: 'brd', name: 'The board', sub: 'the official firmware', c: 'pass' },
    ],
    seq: [
      { phase: 'A release (one time for each version)' },
      { at: 'gh', sign: 'off', title: 'Official OTA key signs', text: 'a tag v1.0.x starts the workflow: it builds each board kind and signs each image' },
      { at: 'gh', sign: 'off', title: 'Official store key signs', text: 'each app and the store index' },
      { from: 'gh', to: 'site', text: 'the release files and the store (GitHub Pages)' },
      { phase: 'Install or update the firmware' },
      { from: 'pc', to: 'site', text: 'open the install page' },
      { from: 'site', to: 'pc', text: 'the files of the board kind', ret: true },
      { at: 'pc', note: 'Checks the size and the SHA-256 of each file (against a damaged download).', title: 'The browser checks' },
      { from: 'pc', to: 'brd', text: 'USB: INSTALL (erase all, then write) or UPDATE' },
      { at: 'brd', note: 'A USB write is direct. The board does not check it: the cable is trusted.', title: 'No check' },
      { at: 'pc', note: 'There are no automatic updates. For a new version, the user runs UPDATE again.', title: 'Later versions', kind: 'hand' },
      { phase: 'Apps from the STORE app of the board' },
      { from: 'brd', to: 'site', text: 'read the store index over WiFi (HTTPS)' },
      { from: 'site', to: 'brd', text: 'index.json + index.sig', ret: true },
      { at: 'brd', title: 'The board checks', check: ['the index is signed by the official store key'], fail: ['else: THE STORE INDEX HAS NO SIGNATURE OF A TRUSTED STORE KEY'] },
      { from: 'brd', to: 'site', text: 'get the files of one app' },
      { from: 'site', to: 'brd', text: 'app.elf, icon, manifest, sig', ret: true },
      { at: 'brd', title: 'The board checks', check: ['each file is the file of the index', 'the app is signed by the official store key', 'the right board kind'] },
      { phase: 'Apps from the store web page (over USB)' },
      { from: 'pc', to: 'site', text: 'open the store page' },
      { at: 'pc', note: 'The page checks index.sig against the official key.', title: 'The page checks' },
      { from: 'pc', to: 'brd', text: 'USB: copy the app to the board' },
      { at: 'brd', title: 'The board checks', check: ['the app is signed by the official store key'] },
    ],
    keys: [
      { k: 'off', n: 'Official store key', priv: 'a secret of the GitHub repo', pub: 'built into the release firmware', signs: 'every app and the store index' },
      { k: 'off', n: 'Official OTA key', priv: 'a secret of the GitHub repo', pub: 'built into the release firmware', signs: 'every firmware image' },
      { k: 'off', n: 'OTA backup key', priv: 'offline, never on GitHub', pub: 'built into the release firmware', signs: 'nothing now; for a key change' },
    ],
    say: 'The firmware signature protects updates over the network (see the pool tab). Over USB the person with the cable can write anything, as on every ESP32 board.',
  },
  {
    id: 'pool', tab: 'Own pool + own apps', color: 'pool',
    title: 'Own pool and own apps: poolmgr and a pool store key',
    intro: 'Someone who runs poolmgr (in the TFN OS source, and a zip in each release) on a computer at home, with several boards, and signs apps of their own. Each board must allow the pool key one time, by hand.',
    parts: [
      { id: 'off', name: 'Official', sub: 'GitHub release + store', c: 'off' },
      { id: 'pm', name: 'poolmgr', sub: 'your computer at home', c: 'pool' },
      { id: 'you', name: 'You', sub: 'at the board', c: 'hand' },
      { id: 'brd', name: 'A pool board', sub: 'the official firmware', c: 'pass' },
    ],
    seq: [
      { phase: 'Trust the pool key (one time for each board)' },
      { at: 'pm', sign: 'pool', title: 'Pool store key', text: 'poolmgr store key makes it (keys/store_pool.pem)' },
      { from: 'pm', to: 'brd', text: 'store trust: the public half of the key' },
      { from: 'brd', to: 'you', text: 'TRUST THE KEY <name>? (for 2 minutes)' },
      { from: 'you', to: 'brd', text: 'a finger on ALLOW (the web cannot allow)' },
      { at: 'brd', note: 'Saved as an added store key (8 at most).', title: 'Saved' },
      { phase: 'Your own apps' },
      { at: 'pm', sign: 'pool', title: 'Pool store key signs', text: 'store publish: each app of yours' },
      { from: 'brd', to: 'pm', text: 'the STORE app reads the poolmgr store (local mode)' },
      { from: 'pm', to: 'brd', text: 'the app files', ret: true },
      { at: 'brd', title: 'The board checks', check: ['signed by the pool store key that you allowed'], fail: ['a board that did not allow it: refused'] },
      { phase: 'Official apps (a copy)' },
      { from: 'off', to: 'pm', text: 'store sync: a copy of the official store' },
      { at: 'pm', note: 'Checks index.sig and each app before it keeps them.', title: 'poolmgr checks' },
      { from: 'pm', to: 'brd', text: 'official app files', ret: true },
      { at: 'brd', title: 'The board checks', check: ['signed by the official store key (built in)'] },
      { phase: 'Firmware updates' },
      { from: 'off', to: 'pm', text: 'a new release' },
      { at: 'pm', note: 'Checks the signature, the board kind and the version first.', title: 'poolmgr checks' },
      { from: 'pm', to: 'brd', text: 'update over the network, one board at a time' },
      { at: 'brd', title: 'The board checks', check: ['signed by the official OTA key', 'the version is not older', 'after the restart: a self-test, else back to the old one'] },
      { at: 'pm', note: 'Stops at the first board that fails.', title: 'Safety' },
    ],
    keys: [
      { k: 'pool', n: 'Pool store key', priv: 'the poolmgr folder: keys/store_pool.pem', pub: 'added on each board after a finger ALLOW', signs: 'your own apps' },
      { k: 'pool', n: 'Pool admin and pool key', priv: 'poolmgr (and an offline recovery key)', pub: 'the member list of the pool', signs: 'members and links between boards, not apps' },
      { k: 'off', n: 'Official keys', priv: 'GitHub', pub: 'built in', signs: 'the firmware and the official apps' },
    ],
    say: 'The pool cannot change the firmware keys: the boards take only official firmware. A board that never allowed the pool key refuses the pool apps.',
  },
  {
    id: 'dev', tab: 'Developer', color: 'dev',
    title: 'A developer: an unsigned app, a developer key, a store countersign',
    intro: 'Someone who writes an app with the SDK. Three ways, from "only my board" to "every board".',
    parts: [
      { id: 'pc', name: 'Developer PC', sub: 'the SDK', c: 'dev' },
      { id: 'st', name: 'A store', sub: 'official or a pool', c: 'off' },
      { id: 'you', name: 'Board owner', sub: 'at the board', c: 'hand' },
      { id: 'brd', name: 'The board', sub: '', c: 'pass' },
    ],
    seq: [
      { phase: '1. Only my own board: no signature' },
      { from: 'you', to: 'brd', text: 'SETUP > DEVELOPER MODE (a finger only; the web can only turn it off)' },
      { at: 'pc', sign: 'dev', title: 'No key', text: 'tfn_app.py build, or mk_app.py --nosign' },
      { from: 'pc', to: 'brd', text: 'push over WiFi' },
      { at: 'brd', title: 'The board decides', check: ['developer mode on: the app runs'], fail: ['developer mode off: refused'] },
      { phase: '2. Testers: a developer key' },
      { at: 'pc', sign: 'dev', title: 'Developer key signs', text: '--dev-key adds the file sigs and your name' },
      { from: 'pc', to: 'brd', text: 'the app file, to a tester’s board' },
      { from: 'you', to: 'brd', text: 'add the developer key as TEST (a finger on ALLOW)' },
      { at: 'brd', title: 'The board decides', check: ['TEST key: runs on this board'], fail: ['else: BY <name>, NOT REVIEWED, does not run'] },
      { phase: '3. Everyone: a store countersigns' },
      { from: 'pc', to: 'st', text: 'send the developer-key app for a review' },
      { at: 'st', sign: 'off', title: 'Store key signs too', text: 'after the review' },
      { from: 'st', to: 'brd', text: 'the board installs it from that store, as usual' },
      { at: 'brd', title: 'The board checks', check: ['signed by a store that the board trusts: runs'] },
    ],
    keys: [
      { k: 'dev', n: 'No key', priv: '-', pub: '-', signs: 'runs only with developer mode on' },
      { k: 'dev', n: 'Developer key', priv: 'your PC (tfn_app.py --dev-key)', pub: 'added on a board as TEST or DEV', signs: 'your app; shows your name' },
      { k: 'off', n: 'A store key', priv: 'the store (official or a pool)', pub: 'built in, or added as a store key', signs: 'the countersign: the app runs everywhere' },
    ],
    say: 'Only a built-in store key can sign the permissions of an app. A developer or pool signature never gives an app more permissions.',
  },
];

// ------------------------------------------------------------------ the sequence renderer
const NS = 'http://www.w3.org/2000/svg';
const L = { W: 1240, M: 24, top: 18, headH: 74, lh: 22, fs: 16 };

function el(name, attrs, parent, text) {
  const e = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs || {})) {
    e.setAttribute(k, v);
  }
  if (text != null) {
    e.textContent = text;
  }
  if (parent) {
    parent.appendChild(e);
  }
  return e;
}

function wrap(text, px, fs) {
  const max = Math.max(8, Math.floor(px / (fs * 0.55)));
  const out = [];
  let cur = '';
  for (const w of String(text).split(' ')) {
    if (cur && (cur + ' ' + w).length > max) {
      out.push(cur);
      cur = w;
    } else {
      cur = cur ? cur + ' ' + w : w;
    }
  }
  if (cur) {
    out.push(cur);
  }
  return out;
}

function drawSeq(s) {
  const n = s.parts.length;
  const sp = (L.W - 2 * L.M) / n;
  const X = (id) => L.M + sp * (s.parts.findIndex((p) => p.id === id) + 0.5);
  const bw = Math.min(sp - 26, 360);
  const svg = el('svg', { role: 'img', 'aria-label': s.title });
  const defs = el('defs', {}, svg);
  for (const [nm, cls] of [['f', 'ahf'], ['r', 'ahr']]) {
    const m = el('marker', { id: `tm-ah${nm}-${s.id}`, viewBox: '0 0 12 12', refX: 11, refY: 6, markerWidth: 9, markerHeight: 9, orient: 'auto-start-reverse' }, defs);
    el('path', { d: 'M0 0 L12 6 L0 12 z', class: cls }, m);
  }
  const gBack = el('g', {}, svg);
  const gLife = el('g', {}, svg);
  const gTop = el('g', {}, svg);

  // the heads of the parts: their own picture, the sticky bar
  const head = el('svg', { viewBox: `0 0 ${L.W} ${L.top + L.headH + 14}`, 'aria-hidden': 'true' });
  for (const p of s.parts) {
    const x = X(p.id);
    const w = Math.min(sp - 20, 250);
    el('line', { x1: x, x2: x, y1: L.top + L.headH, y2: L.top + L.headH + 14, class: 'life' }, head);
    el('rect', { x: x - w / 2, y: L.top, width: w, height: L.headH, rx: 12, class: 'head k-' + p.c }, head);
    el('rect', { x: x - w / 2, y: L.top, width: w, height: 8, rx: 4, class: 'f-' + p.c }, head);
    el('text', { x, y: L.top + 36, class: 'pn', 'text-anchor': 'middle' }, head, p.name);
    if (p.sub) {
      el('text', { x, y: L.top + 58, class: 'ps', 'text-anchor': 'middle' }, head, p.sub);
    }
  }

  let y = 14;
  for (const st of s.seq) {
    if (st.phase) {
      y += 10;
      el('rect', { x: 6, y, width: L.W - 12, height: 40, rx: 8, class: 'phase' }, gBack);
      el('text', { x: 22, y: y + 26, class: 'ph f-' + s.color }, gBack, st.phase.toUpperCase());
      y += 62;
    } else if (st.from) {
      const x1 = X(st.from);
      const x2 = X(st.to);
      const lines = wrap(st.text, Math.abs(x2 - x1) - 30, L.fs);
      const mid = (x1 + x2) / 2;
      lines.forEach((t, i) => el('text', { x: mid, y: y + 16 + i * L.lh, class: 'ml' + (st.ret ? ' ret' : ''), 'text-anchor': 'middle' }, gTop, t));
      y += lines.length * L.lh + 6;
      const dir = x2 > x1 ? 1 : -1;
      el('line', { x1: x1 + dir * 6, x2: x2 - dir * 8, y1: y, y2: y, class: 'msg' + (st.ret ? ' ret' : ''), 'marker-end': `url(#tm-ah${st.ret ? 'r' : 'f'}-${s.id})` }, gTop);
      el('circle', { cx: x1, cy: y, r: 5, class: 'dot' + (st.ret ? ' ret' : '') }, gTop);
      y += 26;
    } else {
      const x = X(st.at);
      const role = st.sign || (st.check ? 'pass' : st.kind === 'hand' ? 'hand' : '');
      const inner = bw - 32;
      const body = [];
      for (const t of st.text ? wrap(st.text, inner, L.fs) : []) {
        body.push({ t });
      }
      for (const t of st.note ? wrap(st.note, inner, L.fs) : []) {
        body.push({ t });
      }
      for (const c of st.check || []) {
        wrap(c, inner - 22, L.fs).forEach((t, i) => body.push({ t, mark: i === 0 ? 'ok' : '', ind: true }));
      }
      for (const c of st.fail || []) {
        wrap(c, inner - 22, L.fs).forEach((t, i) => body.push({ t, mark: i === 0 ? 'no' : '', ind: true, red: true }));
      }
      const h = 14 + (st.title ? 24 : 0) + body.length * L.lh + 12;
      const bx = Math.max(8, Math.min(L.W - 8 - bw, x - bw / 2));
      el('rect', { x: bx, y, width: bw, height: h, rx: 10, class: 'box' + (role ? ' k-' + role : '') }, gTop);
      let ty = y + 14;
      if (st.title) {
        ty += 17;
        el('text', { x: bx + (st.sign ? 42 : 16), y: ty, class: 'bt' + (role ? ' f-' + role : '') }, gTop, st.title);
        if (st.sign) {
          el('circle', { cx: bx + 21, cy: ty - 5, r: 5.5, class: 'keyicon k-' + role }, gTop);
          el('path', { d: `M${bx + 26.5} ${ty - 5} H${bx + 37} M${bx + 33} ${ty - 5} v4.5`, class: 'keyicon k-' + role }, gTop);
        }
        ty += 7;
      }
      for (const b of body) {
        ty += L.lh;
        if (b.mark === 'ok') {
          el('text', { x: bx + 16, y: ty, class: 'bt f-pass' }, gTop, '✓');
        }
        if (b.mark === 'no') {
          el('text', { x: bx + 16, y: ty, class: 'bt f-fail' }, gTop, '✕');
        }
        el('text', { x: bx + (b.ind ? 38 : 16), y: ty, class: 'bl' + (b.red ? ' red' : '') }, gTop, b.t);
      }
      y += h + 18;
    }
  }
  y += 6;
  for (const p of s.parts) {
    el('line', { x1: X(p.id), x2: X(p.id), y1: 0, y2: y, class: 'life' }, gLife);
  }
  svg.setAttribute('viewBox', `0 0 ${L.W} ${y + 6}`);
  const hd = document.createElement('div');
  hd.className = 'tm-head';
  hd.appendChild(head);
  return [hd, svg];
}

// ------------------------------------------------------------------ the panels and the tabs
function node(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) {
    e.className = cls;
  }
  if (text != null) {
    e.textContent = text;
  }
  return e;
}

function keyCards(s) {
  const box = node('div', 'tm-keys');
  for (const k of s.keys) {
    const d = node('div', 'tm-kc k-' + k.k);
    d.appendChild(node('h4', '', k.n));
    const dl = node('dl');
    for (const [dt, dd] of [['Secret half', k.priv], ['On the board', k.pub], ['Signs', k.signs]]) {
      dl.appendChild(node('dt', '', dt));
      dl.appendChild(node('dd', '', dd));
    }
    d.appendChild(dl);
    box.appendChild(d);
  }
  return box;
}

function tag(role, name) {
  const s = node('span', 'tm-tag');
  s.appendChild(node('i', 'tm-c-' + role));
  s.appendChild(document.createTextNode(name));
  return s;
}

function cell(kind, text, span) {
  const td = node('td');
  if (span) {
    td.colSpan = span;
  }
  td.appendChild(node('span', 'tm-' + kind, text));
  return td;
}

function table(head, rows) {
  const wrapEl = node('div', 'tm-tbl');
  const t = node('table');
  const tr = node('tr');
  for (const h of head) {
    tr.appendChild(node('th', '', h));
  }
  t.appendChild(tr);
  for (const r of rows) {
    const row = node('tr');
    const first = node('td');
    first.appendChild(tag(r[0], r[1]));
    row.appendChild(first);
    for (const c of r.slice(2)) {
      row.appendChild(cell(c[0], c[1], c[2]));
    }
    t.appendChild(row);
  }
  wrapEl.appendChild(t);
  return wrapEl;
}

function matrixPanel() {
  const p = node('section', 'tm-panel');
  p.appendChild(node('h2', '', 'Who trusts what'));
  p.appendChild(node('p', 'intro', 'Each row is a signature. Each column is a kind of board. A board accepts a file only when it knows the key.'));
  p.appendChild(node('h3', '', 'Apps'));
  p.appendChild(table(['The app is signed by', 'A board with the official firmware', 'A board that allowed a pool key', 'A board in developer mode'], [
    ['off', 'Official store key', ['y', 'yes'], ['y', 'yes'], ['y', 'yes']],
    ['pool', 'Pool store key', ['n', 'no'], ['y', 'yes, the allowed pool'], ['y', 'yes']],
    ['dev', 'Developer key only', ['q', 'NOT REVIEWED: runs only where the key is added as TEST', 2], ['y', 'yes']],
    ['dev', 'No signature', ['n', 'refused', 2], ['y', 'yes']],
  ]));
  p.appendChild(node('h3', '', 'Firmware updates over the network'));
  p.appendChild(table(['The firmware is signed by', 'A board with the official firmware'], [
    ['off', 'Official OTA key or its backup key', ['y', 'yes']],
    ['dev', 'Any other key', ['n', 'refused: the key is not in its list']],
    ['dev', 'No signature, or a damaged file', ['n', 'refused (the release firmware is in ENFORCE mode)']],
  ]));
  p.appendChild(node('p', 'tm-say', 'Over USB a board takes any firmware: the person with the cable is trusted. The keys are built into each firmware, so a board cannot learn a new firmware key over the network.'));
  return p;
}

const tabs = document.getElementById('tm-tabs');
const panels = document.getElementById('tm-panels');
const all = [];
for (const s of SETUPS) {
  const p = node('section', 'tm-panel');
  p.appendChild(node('h2', '', s.title));
  p.appendChild(node('p', 'intro', s.intro));
  const dia = node('div', 'tm-dia');
  dia.append(...drawSeq(s));
  p.appendChild(dia);
  p.appendChild(node('h3', '', 'The keys of this setup'));
  p.appendChild(keyCards(s));
  p.appendChild(node('p', 'tm-say', s.say));
  all.push({ id: s.id, tab: s.tab, color: s.color, panel: p });
}
all.push({ id: 'matrix', tab: 'Who trusts what', color: 'all', panel: matrixPanel() });

for (const t of all) {
  t.panel.id = 'tm-panel-' + t.id;
  t.panel.setAttribute('role', 'tabpanel');
  t.panel.setAttribute('aria-labelledby', 'tm-tab-' + t.id);
  t.panel.hidden = true;
  panels.appendChild(t.panel);
  const b = node('button', 't-' + t.color);
  b.type = 'button';
  b.id = 'tm-tab-' + t.id;
  b.setAttribute('role', 'tab');
  b.setAttribute('aria-controls', t.panel.id);
  b.appendChild(node('span', 'sw tm-c-' + t.color));
  b.appendChild(document.createTextNode(t.tab));
  b.addEventListener('click', () => {
    show(t.id);
    history.replaceState(null, '', '#' + t.id);
  });
  b.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') {
      return;
    }
    const i = all.indexOf(t);
    const nx = all[(i + (e.key === 'ArrowRight' ? 1 : all.length - 1)) % all.length];
    show(nx.id);
    document.getElementById('tm-tab-' + nx.id).focus();
    history.replaceState(null, '', '#' + nx.id);
  });
  tabs.appendChild(b);
}

function show(id) {
  const pick = all.find((t) => t.id === id) || all[0];
  for (const t of all) {
    const on = t === pick;
    const b = document.getElementById('tm-tab-' + t.id);
    b.setAttribute('aria-selected', on ? 'true' : 'false');
    b.tabIndex = on ? 0 : -1;
    t.panel.hidden = !on;
  }
}

// the sticky names of the parts go under the top bar of the site (its height changes with the width)
function placeHeads() {
  const bar = document.querySelector('header.top');
  if (bar) {
    document.documentElement.style.setProperty('--tm-top', bar.getBoundingClientRect().height + 'px');
  }
}

show(location.hash.slice(1));
window.addEventListener('hashchange', () => show(location.hash.slice(1)));
window.addEventListener('resize', placeHeads);
placeHeads();
