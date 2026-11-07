#pragma once
// The private header of the console (main/console.c and its command files console_*.c): the includes of all of them,
// the commands of the table CON_CMDS (console.c), and the test hooks of the board.
#include <stdio.h>
#include "arc_mac.h"
#include "tfn_extras.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "wifi.h"
#include "tfn_app.h"
#include "tfn_store.h"
#include "bt_pad.h"
#include "imu.h"
#include "board.h"
#include "app.h"
#include "input.h"
#include "esp_timer.h"
#include "gfx.h"
#include "audio.h"
#include "settings.h"
#include "midi_bg.h"
#include "console.h"
#include "storage.h"
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "esp_rom_crc.h"
#include "usb_midi.h"
#include "usb_audio.h"
#include "usb_phones.h"
#include "friends.h"
#include "notify.h"
#include "evlog.h"
#include "watch.h"
#include "lan.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "sysui.h"
#include "ota.h"
#include "layout.h"
#include "vpn.h"
#include "tfn_id.h"
#include "web_auth.h"
#include "device_guard.h"
#include "esp_mac.h"
#include "esp_mmu_map.h"
#include "webcam.h"
#include "power.h"
#include "alarm.h"
#include "oref.h"

// the test hooks of the board (console.c has the empty ones; board.c of the 3.49 has the real ones)
extern int64_t g_touch_log_until_ms;
extern int64_t g_touch2_until_ms;
void board_touch_read_len(int n);
int board_touch_read_len_get(void);
void board_tp_diag(int secs);
int board_tp_reg(int reg, int val);

// the commands (each one gets the first word; it reads the next words with strtok(NULL, ...))
void con_maint(char *cmd);
void con_sdusb(char *cmd);
void con_apps(char *cmd);
void con_pub(char *cmd);  // (main/tfn_pub.c)
void con_wifi(char *cmd);
void con_pad(char *cmd);
void con_padtest(char *cmd);
void con_padcal(char *cmd);
void con_padlog(char *cmd);
void con_notify(char *cmd);
void con_watch(char *cmd);
void con_friends(char *cmd);
void con_lan(char *cmd);
void con_lancopy(char *cmd);
void con_wifistat(char *cmd);
void con_lanonly(char *cmd);
void con_lay(char *cmd);
void con_mmu(char *cmd);
void con_webpass(char *cmd);
void con_vpn(char *cmd);
void con_vpnonly(char *cmd);
void con_finger(char *cmd);
void con_key(char *cmd);
void con_finger2(char *cmd);
void con_shot(char *cmd);
void con_wake(char *cmd);
void con_panel(char *cmd);
void con_kbd(char *cmd);
void con_evlog(char *cmd);
void con_sleepnow(char *cmd);
void con_hotspot(char *cmd);
void con_imu(char *cmd);
void con_imuraw(char *cmd);
void con_bootslot(char *cmd);
void con_orient(char *cmd);
void con_menusel(char *cmd);
void con_menuedit(char *cmd);
void con_open(char *cmd);
void con_wcam(char *cmd);
void con_touchw(char *cmd);
void con_tpdiag(char *cmd);
void con_tpreg(char *cmd);
void con_touchread(char *cmd);
void con_touch2(char *cmd);
void con_touchraw(char *cmd);
void con_touches(char *cmd);
void con_quiet(char *cmd);
void con_snd(char *cmd);
void con_tap(char *cmd);
void con_clock(char *cmd);
void con_oref(char *cmd);
void con_pw(char *cmd);
void con_pmu(char *cmd);
void con_bat(char *cmd);
void con_ls(char *cmd);
void con_ota(char *cmd);
void con_id(char *cmd);
void con_menu(char *cmd);
void con_df(char *cmd);
void con_sdformat(char *cmd);
void con_wheel(char *cmd);
void con_store(char *cmd);
void con_storeall(char *cmd);
void con_storedel(char *cmd);
void con_nvs(char *cmd);
void con_rf(char *cmd);
void con_mem(char *cmd);
void con_ip(char *cmd);
void con_usbaudio(char *cmd);
void con_usbphones(char *cmd);
void con_usbmidi(char *cmd);
