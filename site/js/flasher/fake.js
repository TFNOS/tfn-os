// The test mode of the flasher: a fake serial port and a fake ROM bootloader. They let the page and the Node tests run
// the real detection and the real write plan with no board. Nothing here touches a real port.

const ENC = new TextEncoder();

export class FakeSerialPort {
  // scenario.console: the console answer to "ota" (or null: a board that does not answer)
  // scenario.usb: { usbVendorId, usbProductId }
  // scenario.resetOnOpen: the board starts again when the port opens (bootMs: the time until its console works)
  // The fake chip resets like the real one: when RTS is high and DTR is low.
  constructor(scenario) {
    this.scenario = scenario;
    this.events = [];
    this.isOpen = false;
    this.dtr = false;
    this.rts = false;
    this.resets = 0;
    this.readyAt = 0;
    this.ctrl = null;
    this.readable = null;
    this.writable = null;
  }

  getInfo() {
    return this.scenario.usb || {};
  }

  async open(options) {
    if (this.isOpen) {
      throw new Error('the port is already open');
    }
    this.isOpen = true;
    this.events.push({ ev: 'open', baud: options.baudRate });
    this.readable = new ReadableStream({
      start: (c) => {
        this.ctrl = c;
      },
    });
    this.writable = new WritableStream({ write: (chunk) => this.onWrite(chunk) });
    // (the browser opens the port with DTR and RTS high)
    this.dtr = true;
    this.rts = true;
    if (this.scenario.resetOnOpen) {
      this.chipReset();
    }
  }

  chipReset() {
    this.resets++;
    this.readyAt = Date.now() + (this.scenario.bootMs || 0);
    this.events.push({ ev: 'reset' });
    setTimeout(() => this.enqueue('ESP-ROM:esp32s3-20210327\r\nrst:0x15 (USB_UART_CHIP_RESET)\r\n'), 10);
  }

  enqueue(text) {
    if (this.isOpen && this.ctrl) {
      this.ctrl.enqueue(ENC.encode(text));
    }
  }

  onWrite(chunk) {
    const text = new TextDecoder().decode(chunk);
    this.events.push({ ev: 'write', text });
    if (Date.now() < this.readyAt) {
      return;
    }
    if (text.includes('ota') && this.scenario.console) {
      setTimeout(() => this.enqueue(this.scenario.console), 40);
    }
  }

  // Like the browser: a signal that is not in the object keeps its state.
  async setSignals(signals) {
    if ('dataTerminalReady' in signals) {
      this.dtr = signals.dataTerminalReady;
    }
    if ('requestToSend' in signals) {
      this.rts = signals.requestToSend;
    }
    this.events.push({ ev: 'signals', dtr: this.dtr, rts: this.rts });
    if (this.rts && !this.dtr) {
      this.chipReset();
    }
  }

  async close() {
    this.isOpen = false;
    this.ctrl = null;
    this.events.push({ ev: 'close' });
  }
}

export class FakeEsp {
  // scenario.rom: { chip, flashMB, mac, vid, pid }; scenario.table: the bytes at 0x8000 (Uint8Array)
  constructor(scenario, port, log) {
    this.scenario = scenario;
    this.port = port;
    this.log = log;
    this.writes = null;
    this.eraseAll = null;
    this.resets = 0;
    this.reads = [];
  }

  async connect() {
    if (this.port.isOpen) {
      throw new Error('the console probe did not close the port');
    }
    const r = this.scenario.rom;
    const info = this.port.getInfo();
    this.log('Test mode: the fake ROM bootloader says ' + r.chip + ', ' + r.flashMB + 'MB.');
    return {
      chip: r.chip, description: r.chip + ' (test)', mac: r.mac, flashText: r.flashMB + 'MB',
      flashMB: r.flashMB, vid: info.usbVendorId, pid: info.usbProductId,
    };
  }

  async readFlash(offset, size, onProgress) {
    this.reads.push({ offset, size });
    if (onProgress) {
      onProgress(size, size);
    }
    if (offset === 0x8000 && this.scenario.table) {
      return this.scenario.table.slice(0, size);
    }
    return new Uint8Array(size).fill(0xff);
  }

  async write(fileArray, eraseAll, onProgress) {
    this.eraseAll = eraseAll;
    this.writes = fileArray.map((f) => ({ name: f.name, address: f.address, size: f.data.length }));
    this.log('Test mode: ' + (eraseAll ? 'erase all, then ' : '') + 'write ' +
      this.writes.map((w) => w.name + ' at 0x' + w.address.toString(16) + ' (' + w.size + ' B)').join(', ') + '.');
    fileArray.forEach((f, i) => {
      if (onProgress) {
        onProgress(i, f.data.length, f.data.length);
      }
    });
  }

  async reset() {
    this.resets++;
  }

  async close() {}
}
