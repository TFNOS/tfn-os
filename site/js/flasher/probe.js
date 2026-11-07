// Ask a running TFN OS for its kind and its version, through its USB console, with no reset of the board.
// The browser opens the port with DTR and RTS high. The USB console of the ESP32-S3 and the auto-reset circuit of a
// USB-UART chip both reset the chip when RTS is high and DTR is low. Thus the page sets RTS low first and DTR low
// after it: the reset state never occurs. Then the console command "ota" goes out, and the page reads the answer.
// If a board starts again all the same (the text shows the boot message of the ROM), the page waits for the console
// and sends the command again. The port closes again before the ROM bootloader step.
import { parseConsole } from './core.js';

export const PROBE_MS = 3000;
export const BOOT_MS = 12000;
const SETTLE_MS = 300;
const RESEND_MS = 1000;
const POLL_MS = 50;
const BOOT_MARK = 'ESP-ROM:';

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

// One read loop for the whole probe: all bytes go into sink.text until the reader is cancelled.
async function pump(reader, sink) {
  const dec = new TextDecoder();
  for (;;) {
    const res = await reader.read();
    if (res.done) {
      return;
    }
    sink.text += dec.decode(res.value, { stream: true });
  }
}

async function sendLine(port, text) {
  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(text));
  } finally {
    writer.releaseLock();
  }
}

// Sends "ota" each RESEND_MS until an answer comes. The time limit is PROBE_MS, or BOOT_MS after a boot message.
async function askUntilAnswer(port, sink, log, timing) {
  const start = Date.now();
  let lastSend = 0;
  let bootSeen = false;
  for (;;) {
    if (parseConsole(sink.text)) {
      return;
    }
    if (!bootSeen && sink.text.includes(BOOT_MARK)) {
      bootSeen = true;
      log('Console: the board starts again. The page waits for it.');
    }
    const now = Date.now();
    const limit = bootSeen ? timing.bootMs : timing.probeMs;
    if (now - start >= limit) {
      return;
    }
    if (now - lastSend >= timing.resendMs) {
      lastSend = now;
      await sendLine(port, '\r\nota\r\n');
    }
    await sleep(POLL_MS);
  }
}

// Returns { text, answer } where answer is the result of parseConsole (or null when nothing answered).
// timing (optional, for the tests): { probeMs, bootMs, resendMs, settleMs }.
export async function probeConsole(port, log, timing) {
  const t = Object.assign({ probeMs: PROBE_MS, bootMs: BOOT_MS, resendMs: RESEND_MS, settleMs: SETTLE_MS }, timing);
  await port.open({ baudRate: 115200 });
  const sink = { text: '' };
  let reader = null;
  let loop = null;
  try {
    await port.setSignals({ requestToSend: false });
    await port.setSignals({ dataTerminalReady: false });
    reader = port.readable.getReader();
    loop = pump(reader, sink).catch(() => {});
    await sleep(t.settleMs);
    log('Console: the page sends the command "ota".');
    await askUntilAnswer(port, sink, log, t);
  } finally {
    if (reader) {
      await reader.cancel().catch(() => {});
      await loop;
      reader.releaseLock();
    }
    await port.close();
    await sleep(100);
  }
  return { text: sink.text, answer: parseConsole(sink.text) };
}
