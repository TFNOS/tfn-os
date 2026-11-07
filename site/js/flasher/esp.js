// The ROM bootloader steps over esptool-js: read the chip, the flash and the MAC, read the partition table, save a
// copy of the flash, write the files and start the board again. The test mode uses FakeEsp (fake.js) in its place.
import { ESPLoader, Transport } from '../../vendor/esptool-js-0.7.0/bundle.js';
import { flashMBFromText } from './core.js';
import { md5Hex } from './md5.js';

const BAUD = 460800;
const RESET_MS = 100;

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

export class EspSession {
  constructor(port, log) {
    this.port = port;
    this.log = log;
    this.loader = null;
    this.transport = null;
  }

  async connect() {
    const log = this.log;
    const terminal = { clean() {}, writeLine: (s) => log(s), write: (s) => log(s) };
    this.transport = new Transport(this.port, false);
    this.loader = new ESPLoader({ transport: this.transport, baudrate: BAUD, romBaudrate: 115200, terminal });
    const description = await this.loader.main('default_reset');
    const chip = this.loader.chip.CHIP_NAME;
    const mac = await this.loader.chip.readMac(this.loader);
    const flashText = (await this.loader.detectFlashSize()) || '';
    const info = this.port.getInfo ? this.port.getInfo() : {};
    return {
      chip, description, mac, flashText, flashMB: flashMBFromText(flashText),
      vid: info.usbVendorId, pid: info.usbProductId,
    };
  }

  async readFlash(offset, size, onProgress) {
    return this.loader.readFlash(offset, size, (packet, got, total) => {
      if (onProgress) {
        onProgress(got, total);
      }
    });
  }

  async write(fileArray, eraseAll, onProgress) {
    await this.loader.writeFlash({
      fileArray: fileArray.map((f) => ({ data: f.data, address: f.address })),
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll,
      compress: true,
      reportProgress: onProgress,
      calculateMD5Hash: md5Hex,
    });
  }

  // A hard reset: RTS high and DTR low hold the chip in reset for 100 ms. Then RTS goes low, and the chip starts the
  // app (DTR low keeps GPIO0 high). The hard reset of esptool-js 0.7.0 only sets RTS low, with no pulse, so the board
  // stays in the download mode. The setRTS of the Transport also sends the DTR state again: some Windows USB drivers
  // send a new RTS state only with a DTR change. The port can go away during the reset (USB console of the chip).
  async reset() {
    const t = this.transport;
    this.log('Hard reset with the RTS pin.');
    await t.setDTR(false);
    await t.setRTS(true);
    await sleep(RESET_MS);
    await t.setRTS(false).catch(() => {});
  }

  async close() {
    if (this.transport) {
      await this.transport.disconnect().catch(() => {});
    }
  }
}
