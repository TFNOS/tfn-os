// The board kinds of TFN OS: the facts that the flasher needs to find and check a board.
// A kind name is the same text that the firmware prints in the console line of "ota" ("board <kind>, ...").

export const ESPRESSIF_VID = 0x303a;
export const WCH_VID = 0x1a86;

export const BOARDS = [
  {
    kind: 'lcd349',
    name: 'Waveshare ESP32-S3-Touch-LCD-3.49',
    short: 'ESP32-S3-Touch-LCD-3.49',
    chip: 'ESP32-S3',
    flashMB: 16,
    usb: [{ vid: ESPRESSIF_VID, pid: 0x1001 }],
    screen: '640 x 172 bar',
    shot: 'img/screens/349_menu.webp',
    web: true,
  },
  {
    kind: 'cores3',
    name: 'M5Stack CoreS3 / CoreS3 SE',
    short: 'CoreS3 / CoreS3 SE',
    chip: 'ESP32-S3',
    flashMB: 16,
    usb: [{ vid: ESPRESSIF_VID, pid: 0x1001 }],
    screen: '320 x 240',
    shot: 'img/screens/s3_menu.webp',
    web: true,
  },
  {
    kind: 'am18',
    name: 'Waveshare ESP32-S3-Touch-AMOLED-1.8',
    short: 'ESP32-S3-Touch-AMOLED-1.8',
    chip: 'ESP32-S3',
    flashMB: 16,
    usb: [{ vid: ESPRESSIF_VID, pid: 0x1001 }],
    screen: '368 x 448 AMOLED',
    shot: 'img/screens/am18_murano.webp',
    web: true,
  },
  {
    kind: 'am175',
    name: 'Waveshare ESP32-S3-Touch-AMOLED-1.75C',
    short: 'ESP32-S3-Touch-AMOLED-1.75C',
    chip: 'ESP32-S3',
    flashMB: 32,
    usb: [{ vid: ESPRESSIF_VID, pid: 0x1001 }],
    screen: '466 x 466 round AMOLED',
    shot: 'img/screens/a175_menu.webp',
    web: true,
  },
  {
    kind: 'watcher',
    name: 'Seeed SenseCAP Watcher',
    short: 'SenseCAP Watcher',
    chip: 'ESP32-S3',
    flashMB: 32,
    usb: [{ vid: WCH_VID, pid: 0x55d2 }],
    screen: '412 x 412 round',
    shot: 'img/screens/wt_menu.webp',
    web: true,
  },
  {
    kind: 'tab5',
    name: 'M5Stack Tab5',
    short: 'Tab5',
    chip: 'ESP32-P4',
    flashMB: 16,
    usb: [{ vid: ESPRESSIF_VID, pid: 0x1001 }],
    screen: '1280 x 720',
    shot: 'img/screens/t5_menu.webp',
    web: false,
  },
];

export function boardByKind(kind) {
  return BOARDS.find((b) => b.kind === kind) || null;
}
