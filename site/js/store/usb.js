// The USB link of the store page: one open port to the console of a running TFN OS for the whole visit. The page
// asks the board what it is ("ota", "apps"), sends the files of an app with "sdusb put" (base64 chunks, each answer
// gives the new end of the file), checks each file with "sdusb stat" (the size and the CRC-32), then installs it with
// "apps commit". The board checks the signature of the app at the install, the same as for each other install.
// The port opens as in flasher/probe.js: RTS low first, then DTR low, so that the board does not start again.
import { parseConsole } from '../flasher/core.js';
import {
  base64, crc32, FILES, hexText, leNum, parseApps, parseCommit, parseStoreSource, parseUsbfs, usbTarget,
} from './core.js';

export const CHUNK = 384;        // (one console line holds 767 characters: 384 bytes = 512 base64 characters)
const TRIES = 4;
export const WINDOW = 1;        // (the chunks in flight: the receive buffer of the board console is 1 KB, and
//                                  lines that come faster than the console uses them get lost: see putFile)
const POLL_MS = 15;
const KEEP = 262144;             // (the text that the page keeps of the console)

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

export class BoardLink {
  constructor(port, log) {
    this.port = port;
    this.log = log || (() => {});
    this.text = '';
    this.base = 0;               // (the number of characters that the page threw away before this.text)
    this.rid = 0;
    this.reader = null;
    this.loop = null;
    this.lost = false;
    this.busy = Promise.resolve();
    this.window = WINDOW;
    this.wake = null;
  }

  async open() {
    await this.port.open({ baudRate: 115200 });
    await this.port.setSignals({ requestToSend: false });
    await this.port.setSignals({ dataTerminalReady: false });
    this.reader = this.port.readable.getReader();
    const dec = new TextDecoder();
    this.loop = (async () => {
      for (;;) {
        const res = await this.reader.read();
        if (res.done) {
          return;
        }
        this.text += dec.decode(res.value, { stream: true });
        if (this.wake) {
          const w = this.wake;
          this.wake = null;
          w();
        }
        if (this.text.length > 2 * KEEP) {
          this.base += this.text.length - KEEP;
          this.text = this.text.slice(-KEEP);
        }
      }
    })().catch(() => {
      this.lost = true;
    });
    await sleep(300);
  }

  async close() {
    if (this.reader) {
      await this.reader.cancel().catch(() => {});
      await this.loop;
      this.reader.releaseLock();
      this.reader = null;
    }
    await this.port.close().catch(() => {});
  }

  // Wait until new data comes from the board, or ms.
  waitData(ms) {
    return new Promise((r) => {
      const t = setTimeout(() => {
        this.wake = null;
        r();
      }, ms);
      this.wake = () => {
        clearTimeout(t);
        r();
      };
    });
  }

  mark() {
    return this.base + this.text.length;
  }

  since(mark) {
    return this.text.slice(Math.max(0, mark - this.base));
  }

  async send(line) {
    const writer = this.port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(line + '\n'));
    } finally {
      writer.releaseLock();
    }
  }

  // Send a command and wait until test(the text after it) gives a value (or the time ends: null).
  async ask(line, test, ms, resendMs) {
    const mark = this.mark();
    let sent = 0;
    const end = Date.now() + ms;
    for (;;) {
      if (this.lost) {
        throw new Error('The USB connection to the board is lost.');
      }
      if (!sent || (resendMs && Date.now() - sent >= resendMs)) {
        sent = Date.now();
        await this.send(line);
      }
      const v = test(this.since(mark));
      if (v) {
        return v;
      }
      if (Date.now() >= end) {
        return null;
      }
      await this.waitData(POLL_MS);
    }
  }

  // One request at a time: an install and a check of the board never mix their lines.
  run(job) {
    const next = this.busy.then(job, job);
    this.busy = next.catch(() => {});
    return next;
  }

  async usbfs(args, ms) {
    const rid = (++this.rid).toString(16).padStart(8, '0');
    const p = await this.ask('sdusb ' + rid + ' ' + args, (t) => parseUsbfs(t, rid), ms || 5000);
    return p || new TextEncoder().encode('!TIMEOUT');
  }

  // What the board is: { kind, version, apps, abi, count, card, store } (or null: no TFN OS answers).
  hello() {
    return this.run(async () => {
      await this.send('');
      const tfn = await this.ask('ota', (t) => parseConsole(t), 4000, 1000);
      if (!tfn || !tfn.kind) {
        return tfn && tfn.protectedMode ? { protectedMode: true } : null;
      }
      let list = null;
      for (let i = 0; i < 2 && (!list || Object.keys(list.apps).length < list.count); i++) {
        // (a long answer can lose a line: ask again one time)
        list = await this.ask('apps', (t) => (parseApps(t).abi >= 0 ? parseApps(t) : null), 5000);
      }
      const ls = await this.usbfs('ls ' + hexText('/') + ' 0');
      const card = !(ls[0] === 0x21 && new TextDecoder().decode(ls.slice(1)) === 'NO_CARD');
      const store = await this.ask('apps src', (t) => parseStoreSource(t), 3000);
      return {
        kind: tfn.kind, version: tfn.version, card, store,
        apps: list ? list.apps : {}, abi: list ? list.abi : -1, count: list ? list.count : -1,
      };
    });
  }

  // The installed apps again (after an install).
  apps() {
    return this.run(async () => this.ask('apps', (t) => (parseApps(t).abi >= 0 ? parseApps(t) : null), 5000));
  }

  // A board of TFN OS 1.0.0 has the old address of the static store (the repo moved to the TFNOS org): put the
  // address of this store in its place. The mode stays.
  fixStore(url) {
    return this.run(async () => {
      const m = await this.ask('apps src auto', (t) => parseStoreSource(t), 3000);
      const mode = m ? m.mode : 'auto';
      return this.ask('apps src ' + mode + ' ' + url, (t) => parseStoreSource(t), 3000);
    });
  }

  // One chunk at the offset, with TRIES tries. The answer has the new end of the file: a line that lost characters
  // gives less, or a fault: send it again.
  async putChunk(hp, path, data, off) {
    const part = data.subarray(off, off + CHUNK);
    let r = null;
    for (let i = 0; i < TRIES; i++) {
      r = await this.usbfs('put ' + hp + ' ' + off + ' ' + base64(part));
      if (r[0] === 0x57 && leNum(r, 1, 8) === off + part.length) {
        return;
      }
    }
    throw new Error(path + ': ' + (r[0] === 0x21 ? new TextDecoder().decode(r.slice(1)) : 'a short write'));
  }

  // Write data into the file path (a path of usb_fs.inc) and check it. progress(bytes done).
  // The first chunk goes alone (it makes the file new). Then this.window chunks go before their answers come: each
  // chunk names its offset, so the order of the writes does not matter, and a chunk with no good answer goes again
  // alone. (A test on the AMOLED-1.8: 2 chunks in flight were slower than 1, 4 stopped: the console
  // lost lines. So the window is 1; the gain came from the wait that ends at the answer.)
  async putFile(path, data, progress) {
    const hp = hexText(path);
    const w = Math.max(1, this.window || 1);
    await this.putChunk(hp, path, data, 0);
    progress(Math.min(CHUNK, data.length));
    for (let off = CHUNK; off < data.length;) {
      const batch = [];
      const mark = this.mark();
      for (let k = 0; k < w && off < data.length; k++, off += CHUNK) {
        const part = data.subarray(off, off + CHUNK);
        const rid = (++this.rid).toString(16).padStart(8, '0');
        batch.push({ rid, off, end: off + part.length });
        await this.send('sdusb ' + rid + ' put ' + hp + ' ' + off + ' ' + base64(part));
      }
      for (const c of batch) {
        let r = null;
        const until = Date.now() + 5000;
        while (!(r = parseUsbfs(this.since(mark), c.rid)) && Date.now() < until && !this.lost) {
          await this.waitData(POLL_MS);
        }
        if (!(r && r[0] === 0x57 && leNum(r, 1, 8) === c.end)) {
          await this.putChunk(hp, path, data, c.off);
        }
      }
      progress(Math.min(off, data.length));
    }
    await this.usbfs('close');
    const s = await this.usbfs('stat ' + hp, 30000 + data.length / 100);
    if (s[0] !== 0x53 || leNum(s, 1, 8) !== data.length || leNum(s, 9, 4) !== crc32(data)) {
      throw new Error(path + ': the check of the file on the board failed');
    }
  }

  // Install one app: files = { name: Uint8Array } (the files of the package). progress(done, total).
  install(id, files, card, installed, progress) {
    return this.run(async () => {
      const t = usbTarget(id, card, installed);
      const names = FILES.filter((n) => files[n]);
      const total = names.reduce((s, n) => s + files[n].length, 0);
      let done = 0;
      for (const n of names) {
        const before = done;
        await this.putFile(t.dir + n, files[n], (b) => progress(before + b, total));
        done += files[n].length;
      }
      const r = await this.ask(t.commit, (x) => parseCommit(x, id), 60000);
      if (!r) {
        throw new Error('The board did not answer the install of ' + id + '.');
      }
      if (!r.ok) {
        throw new Error('The board refused the app: ' + (r.msg || 'no reason') + '.');
      }
      return t;
    });
  }

  // Copy the files of a data pack to the SD card: files = [{ path: '/wood/a.bin', data }]. progress(done, total).
  pack(files, progress) {
    return this.run(async () => {
      const total = files.reduce((s, f) => s + f.data.length, 0);
      let done = 0;
      for (const f of files) {
        const before = done;
        // (a file that is on the card with the same size and CRC stays: an update or a second try is fast)
        const s = await this.usbfs('stat ' + hexText(f.path), 30000);
        if (!(s[0] === 0x53 && leNum(s, 1, 8) === f.data.length && leNum(s, 9, 4) === crc32(f.data))) {
          await this.putFile(f.path, f.data, (b) => progress(before + b, total));
        }
        done += f.data.length;
        progress(done, total);
      }
    });
  }
}
