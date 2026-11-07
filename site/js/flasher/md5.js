// MD5 of a Uint8Array as hex text. esptool-js compares it with the MD5 that the board reads back after each write.
// (Web Crypto has no MD5. MD5 is here only a check of the transfer, not a security check: SHA-256 checks the files.)

const S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
const K = new Uint32Array(64);
for (let i = 0; i < 64; i++) {
  K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000) >>> 0;
}

function shiftOf(i) {
  return S[(i >> 4) * 4 + (i & 3)];
}

function mixOf(i, b, c, d) {
  if (i < 16) {
    return [(b & c) | (~b & d), i];
  }
  if (i < 32) {
    return [(d & b) | (~d & c), (5 * i + 1) & 15];
  }
  if (i < 48) {
    return [b ^ c ^ d, (3 * i + 5) & 15];
  }
  return [c ^ (b | ~d), (7 * i) & 15];
}

function block(h, w) {
  let a = h[0];
  let b = h[1];
  let c = h[2];
  let d = h[3];
  for (let i = 0; i < 64; i++) {
    const [f, g] = mixOf(i, b, c, d);
    const t = d;
    d = c;
    c = b;
    const x = (a + f + K[i] + w[g]) >>> 0;
    const s = shiftOf(i);
    b = (b + ((x << s) | (x >>> (32 - s)))) >>> 0;
    a = t;
  }
  h[0] = (h[0] + a) >>> 0;
  h[1] = (h[1] + b) >>> 0;
  h[2] = (h[2] + c) >>> 0;
  h[3] = (h[3] + d) >>> 0;
}

export function md5Hex(data) {
  const n = data.length;
  const total = (((n + 8) >> 6) + 1) * 64;
  const buf = new Uint8Array(total);
  buf.set(data);
  buf[n] = 0x80;
  const view = new DataView(buf.buffer);
  view.setUint32(total - 8, (n * 8) >>> 0, true);
  view.setUint32(total - 4, Math.floor(n / 0x20000000), true);
  const h = new Uint32Array([0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]);
  const w = new Uint32Array(16);
  for (let off = 0; off < total; off += 64) {
    for (let j = 0; j < 16; j++) {
      w[j] = view.getUint32(off + j * 4, true);
    }
    block(h, w);
  }
  const out = new DataView(h.buffer);
  let hexText = '';
  for (let i = 0; i < 16; i++) {
    hexText += out.getUint8(i).toString(16).padStart(2, '0');
  }
  return hexText;
}
