// The logic of the flasher with no browser parts: read the console answer of TFN OS, find the boards that fit a chip,
// make the write plan from a manifest, and check the files. The tests (test/flasher.test.js) run this file in Node.
import { BOARDS, boardByKind } from '../boards.js';

export const APP_OFFSET = 0x10000;
export const TABLE_OFFSET = 0x8000;
export const MODES = ['install', 'update'];

const ANSI = /\x1b\[[0-9;]*[A-Za-z]/g;
const OTA_LINE = /board ([a-z0-9]+), firmware ([^\s,]+), running ([^\s,]+)/;

export function stripAnsi(text) {
  return text.replace(ANSI, '');
}

// The console answer of the command "ota": "board <kind>, firmware <version>, running <slot> ...".
// A board in the protected mode answers "ADMIN REQUIRED" and tells nothing.
export function parseConsole(text) {
  const clean = stripAnsi(text || '');
  const m = clean.match(OTA_LINE);
  if (m) {
    return { kind: m[1], version: m[2], slot: m[3], known: !!boardByKind(m[1]) };
  }
  if (clean.includes('ADMIN REQUIRED')) {
    return { protectedMode: true };
  }
  return null;
}

export function flashMBFromText(text) {
  const m = /^(\d+)MB$/.exec(text || '');
  if (m) {
    return Number(m[1]);
  }
  const k = /^(\d+)KB$/.exec(text || '');
  if (k) {
    return Number(k[1]) / 1024;
  }
  return 0;
}

function usbFits(board, vid, pid) {
  if (vid === undefined || vid === null) {
    return true;
  }
  return board.usb.some((u) => u.vid === vid && u.pid === pid);
}

// The boards that fit what the ROM bootloader and the USB port tell. The chip and the flash size must be the same.
export function shortlist(rom) {
  return BOARDS.filter((b) => b.chip === rom.chip && b.flashMB === rom.flashMB && usbFits(b, rom.vid, rom.pid));
}

// The result of the detection: the boards that the user can choose, and whether an UPDATE is possible.
export function decide(tfn, rom) {
  if (tfn && tfn.kind) {
    const board = boardByKind(tfn.kind);
    if (!board) {
      return { error: 'The board runs TFN OS for "' + tfn.kind + '", a kind that this page does not know.', candidates: [] };
    }
    if (rom && (rom.chip !== board.chip || rom.flashMB < board.flashMB)) {
      return { error: 'The board says "' + tfn.kind + '", but its chip is ' + rom.chip + ' with ' + rom.flashMB +
        ' MB flash. The page stops.', candidates: [] };
    }
    return { candidates: [board], detected: board, canUpdate: board.web };
  }
  if (!rom) {
    return { error: 'The page could not read the chip of the board.', candidates: [] };
  }
  const list = shortlist(rom);
  if (!list.length) {
    return { error: 'No TFN OS board uses the chip ' + rom.chip + ' with ' + rom.flashMB + ' MB flash on this kind of USB port.' +
      ' This page cannot install TFN OS on this board.', candidates: [] };
  }
  return { candidates: list, detected: list.length === 1 ? list[0] : null, canUpdate: false };
}

function fileError(f, flashBytes) {
  if (!Number.isInteger(f.offset) || f.offset < 0 || f.offset % 0x1000 !== 0) {
    return f.name + ': the offset is not on a 4 KB sector';
  }
  if (!Number.isInteger(f.size) || f.size <= 0) {
    return f.name + ': no size';
  }
  if (f.offset + f.size > flashBytes) {
    return f.name + ': it goes past the end of the flash';
  }
  if (!/^[0-9a-f]{64}$/.test(f.sha256 || '')) {
    return f.name + ': no SHA-256';
  }
  return '';
}

// The write plan of one mode. INSTALL writes all files and erases the whole flash first. UPDATE writes only the app
// and the otadata (the otadata makes the board start the new app), so the settings and the files stay.
export function buildPlan(manifest, mode, board) {
  const errors = [];
  if (!MODES.includes(mode)) {
    errors.push('unknown mode ' + mode);
  }
  if (!manifest || manifest.kind !== board.kind) {
    errors.push('the manifest is not for ' + board.kind);
  }
  if (manifest && manifest.chip !== board.chip) {
    errors.push('the manifest is for the chip ' + manifest.chip);
  }
  if (errors.length) {
    return { errors, writes: [] };
  }
  const flashBytes = board.flashMB * 1024 * 1024;
  const writes = manifest.files.filter((f) => f.modes.includes(mode)).sort((a, b) => a.offset - b.offset);
  for (const f of writes) {
    const e = fileError(f, flashBytes);
    if (e) {
      errors.push(e);
    }
  }
  for (let i = 1; i < writes.length; i++) {
    if (writes[i - 1].offset + writes[i - 1].size > writes[i].offset) {
      errors.push(writes[i - 1].name + ' and ' + writes[i].name + ' overlap');
    }
  }
  if (!writes.some((f) => f.name === 'app' && f.offset === APP_OFFSET)) {
    errors.push('the plan has no app at 0x10000');
  }
  if (mode === 'install' && !writes.some((f) => f.name === 'bootloader')) {
    errors.push('the install plan has no bootloader');
  }
  if (mode === 'update' && writes.some((f) => f.name === 'bootloader' || f.name === 'partition-table')) {
    errors.push('an update must not write the bootloader or the partition table');
  }
  const table = manifest.files.find((f) => f.name === 'partition-table');
  return {
    errors, writes, mode, eraseAll: mode === 'install', version: manifest.version, kind: board.kind,
    table: table ? { offset: table.offset, size: table.size, sha256: table.sha256 } : null,
  };
}

export function hex(n, width = 6) {
  return '0x' + n.toString(16).padStart(width, '0');
}

export async function sha256Hex(bytes) {
  const d = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('');
}

// Get each file of the plan and check its size and its SHA-256 before the write. fetchBytes(path) gives a Uint8Array.
export async function loadPlanFiles(plan, base, fetchBytes, onFile) {
  const out = [];
  for (const f of plan.writes) {
    const data = await fetchBytes(base + f.path + '?sha=' + f.sha256.slice(0, 12));
    if (data.length !== f.size) {
      throw new Error(f.name + ': the file has ' + data.length + ' bytes, the manifest says ' + f.size);
    }
    const got = await sha256Hex(data);
    if (got !== f.sha256) {
      throw new Error(f.name + ': the SHA-256 is not the same as in the manifest');
    }
    if (onFile) {
      onFile(f, got);
    }
    out.push({ data, address: f.offset, name: f.name });
  }
  return out;
}

// UPDATE keeps the partition table of the board. It must be the same table as in the release.
export async function tableMatches(plan, tableBytes) {
  if (!plan.table) {
    return false;
  }
  return (await sha256Hex(tableBytes.slice(0, plan.table.size))) === plan.table.sha256;
}
