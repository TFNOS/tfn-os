// The test scenarios of the flasher: what a board says on its console, what its ROM bootloader says, and what the
// page must do. test/run.js checks them (in the browser at test.html, and in Node).
import { ESPRESSIF_VID, WCH_VID } from '../boards.js';

const NATIVE = { usbVendorId: ESPRESSIF_VID, usbProductId: 0x1001 };
const WCH = { usbVendorId: WCH_VID, usbProductId: 0x55d2 };
const S3_16 = { chip: 'ESP32-S3', flashMB: 16, mac: '02:00:00:00:00:01' };
const S3_32 = { chip: 'ESP32-S3', flashMB: 32, mac: '02:00:00:00:00:02' };
const P4_16 = { chip: 'ESP32-P4', flashMB: 16, mac: '02:00:00:00:00:03' };

function otaLine(kind, version) {
  return '\x1b[0;32mI (812345) ota: board ' + kind + ', firmware ' + version + ', running ota_0 (state 2), next boot ' +
    'ota_0, update slot ota_1, build Nov  2 2026 22:38:00 0123abcd\x1b[0m\r\n';
}

const INSTALL4 = ['bootloader@0x000000', 'partition-table@0x008000', 'app@0x010000', 'otadata@0x710000'];
const UPDATE2 = ['app@0x010000', 'otadata@0x710000'];

export const SCENARIOS = {
  'tfn-lcd349': {
    label: 'TFN OS on a 3.49 (console answers)',
    console: otaLine('lcd349', '1.0.79'), usb: NATIVE, rom: S3_16, table: 'same',
    expect: { kind: 'lcd349', candidates: ['lcd349'], canUpdate: true, update: UPDATE2, install: INSTALL4 },
  },
  'tfn-restart': {
    label: 'TFN OS on a 3.49 that starts again when the port opens',
    console: otaLine('lcd349', '1.0.79'), usb: NATIVE, rom: S3_16, table: 'same', resetOnOpen: true, bootMs: 1500,
    expect: { kind: 'lcd349', candidates: ['lcd349'], canUpdate: true, update: UPDATE2, install: INSTALL4, resets: 1 },
  },
  'tfn-am175': {
    label: 'TFN OS on a 1.75C (console answers)',
    console: otaLine('am175', '1.0.79'), usb: NATIVE, rom: S3_32, table: 'same',
    expect: { kind: 'am175', candidates: ['am175'], canUpdate: true, update: UPDATE2, install: INSTALL4 },
  },
  'tfn-other-table': {
    label: 'TFN OS on a CoreS3 with an old partition table',
    console: otaLine('cores3', '1.0.40'), usb: NATIVE, rom: S3_16, table: 'other',
    expect: { kind: 'cores3', candidates: ['cores3'], canUpdate: true, updateRefused: true, install: INSTALL4 },
  },
  'tfn-wrong-chip': {
    label: 'The console says lcd349, the chip is an ESP32-P4',
    console: otaLine('lcd349', '1.0.79'), usb: NATIVE, rom: P4_16, table: null,
    expect: { error: true, candidates: [] },
  },
  'protected': {
    label: 'A board in the protected mode',
    console: 'ADMIN REQUIRED\r\n', usb: NATIVE, rom: S3_16, table: null,
    expect: { candidates: ['lcd349', 'cores3', 'am18'], canUpdate: false, install: INSTALL4 },
  },
  'rom-s3-16': {
    label: 'No TFN OS: ESP32-S3, 16 MB, native USB',
    console: null, usb: NATIVE, rom: S3_16, table: null,
    expect: { candidates: ['lcd349', 'cores3', 'am18'], canUpdate: false, install: INSTALL4 },
  },
  'rom-s3-32-native': {
    label: 'No TFN OS: ESP32-S3, 32 MB, native USB',
    console: null, usb: NATIVE, rom: S3_32, table: null,
    expect: { kind: 'am175', candidates: ['am175'], canUpdate: false, install: INSTALL4 },
  },
  'rom-s3-32-wch': {
    label: 'No TFN OS: ESP32-S3, 32 MB, a WCH USB-UART chip',
    console: null, usb: WCH, rom: S3_32, table: null,
    expect: { kind: 'watcher', candidates: ['watcher'], canUpdate: false, install: INSTALL4 },
  },
  'rom-p4': {
    label: 'No TFN OS: ESP32-P4 (the Tab5)',
    console: null, usb: NATIVE, rom: P4_16, table: null,
    expect: { kind: 'tab5', candidates: ['tab5'], canUpdate: false, install: INSTALL4 },
  },
  'rom-esp32': {
    label: 'An ESP32 (not S3): no TFN OS board',
    console: null, usb: NATIVE, rom: { chip: 'ESP32', flashMB: 4, mac: '02:00:00:00:00:04' }, table: null,
    expect: { error: true, candidates: [] },
  },
};
