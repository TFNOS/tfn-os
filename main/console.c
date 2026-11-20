// A few commands on the USB serial console, for set-up without the screen keyboard. This file has the table of the
// commands, the line parser and the task; the commands are in console_net.c, console_input.c, console_usb.c and
// console_sys.c (console_priv.h).

#include "console_priv.h"
#include "tfn_pool.h"

int64_t g_touch_log_until_ms;  // (board.c on the 3.49: "touchw" logs the touch weight and area until this time)
int64_t g_touch2_until_ms;     // (board.c on the 3.49: "touch2" logs each report with the ids until this time)
// (the 3.49 board.c has the touch read length; the other boards: no such setting)
__attribute__((weak)) void board_touch_read_len(int n)
{
    (void)n;
}
__attribute__((weak)) int board_touch_read_len_get(void)
{
    return 0;
}
__attribute__((weak)) void board_tp_diag(int secs)
{
    (void)secs;
}
__attribute__((weak)) int board_tp_reg(int reg, int val)
{
    (void)reg;
    (void)val;
    return -1;
}

static const char *TAG = "console";

// The commands of the console, in the order of the old test chain (the first match runs).
typedef struct {
    const char *name;
    void (*fn)(char *cmd);
} con_cmd_t;

static const con_cmd_t CON_CMDS[] = {
#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
        {"maint", con_maint},
#endif
        {"sdusb", con_sdusb},
        {"apps", con_apps},
        {"pub", con_pub},
        {"wifi", con_wifi},
        {"pad", con_pad},
        {"padtest", con_padtest},
        {"padcal", con_padcal},
        {"padlog", con_padlog},
        {"notify", con_notify},
        {"watch", con_watch},
        {"friends", con_friends},
        {"lan", con_lan},
        {"lancopy", con_lancopy},
        {"wifistat", con_wifistat},
        {"lanonly", con_lanonly},
        {"lay", con_lay},
        {"mmu", con_mmu},
        {"webpass", con_webpass},
#if CONFIG_TFN_EXTRA_VPN
        {"vpn", con_vpn},
        {"vpnonly", con_vpnonly},
#endif
        {"finger", con_finger},
        {"fmove", con_finger},
        {"key", con_key},
        {"finger2", con_finger2},
        {"fmove2", con_finger2},
        {"shot", con_shot},
        {"wake", con_wake},
        {"panel", con_panel},
        {"dispoff", con_dispoff},
        {"kbd", con_kbd},
        {"evlog", con_evlog},
        {"sleepnow", con_sleepnow},
        {"hotspot", con_hotspot},
        {"imu", con_imu},
        {"imuraw", con_imuraw},
#if BOARD_BOOT_MENU && CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC
        {"bootslot", con_bootslot},
#endif
        {"orient", con_orient},
        {"menusel", con_menusel},
        {"menuedit", con_menuedit},
        {"open", con_open},
#if CONFIG_TFN_EXTRA_WEBCAM
        {"wcam", con_wcam},
#endif
        {"touchw", con_touchw},
        {"tpdiag", con_tpdiag},
        {"tpreg", con_tpreg},
        {"tpset", con_tpreg},
        {"touchread", con_touchread},
        {"touch2", con_touch2},
        {"touchraw", con_touchraw},
        {"touches", con_touches},
        {"quiet", con_quiet},
        {"say", con_say},
        {"listen", con_listen},
        {"snd", con_snd},
        {"tap", con_tap},
        {"ltap", con_tap},
        {"ldown", con_tap},
        {"lup", con_tap},
        {"clock", con_clock},
        {"standby", con_standby},
#if CONFIG_TFN_EXTRA_OREF
        {"oref", con_oref},
#endif
        {"pw", con_pw},
#if BOARD_PMU_LOG
        {"pmu", con_pmu},
#endif
        {"bat", con_bat},
        {"ls", con_ls},
        {"ota", con_ota},
        {"id", con_id},
        {"pool", con_pool},
        {"menu", con_menu},
        {"df", con_df},
        {"sdformat", con_sdformat},
#if BOARD_WHEEL
        {"wheel", con_wheel},
#endif
        {"store", con_store},
        {"storeall", con_storeall},
        {"storedel", con_storedel},
        {"nvs", con_nvs},
#if CONFIG_TFN_EXTRA_RF_NATIVE
        {"rf", con_rf},
#endif
        {"mem", con_mem},
        {"ip", con_ip},
        {"usbaudio", con_usbaudio},
        {"usbphones", con_usbphones},
        {"usbmidi", con_usbmidi},
};

static void run(char *line)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    {
        const char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        size_t n = strcspn(p, " \t\r\n");
        if (!n) {
            return;
        }
        if (n >= 32) {
            puts("ADMIN REQUIRED");
            return;
        }
        char command[32];
        memcpy(command, p, n);
        command[n] = 0;
        p += n;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!device_guard_console(command, p)) {
            return;
        }
    }
#endif
    char *cmd = strtok(line, " \t\r\n");
    if (!cmd) {
        return;
    }
    for (size_t i = 0; i < sizeof(CON_CMDS) / sizeof(CON_CMDS[0]); i++) {
        if (!strcmp(cmd, CON_CMDS[i].name)) {
            CON_CMDS[i].fn(cmd);
            return;
        }
    }
    ESP_LOGI(TAG, "commands: wifi <name> <password>, wifi forget, ip, ls [folder], df, sdusb");
}

static void console_task(void *arg)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    static char line[2304];  // authenticated maintenance can carry a 512-byte hex chunk
#else
    char line[768];
#endif
    int n = 0;
    bool overflow = false;
#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
    // (the USB reads come in blocks: one driver call for each byte drains the receive buffer too slowly,
    // and a long line (sdusb put) loses characters)
    uint8_t blk[64];
    int bn = 0, bi = 0;
#endif
    for (;;) {
        uint8_t c;
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
#else
        if (bi >= bn) {
            bn = usb_serial_jtag_read_bytes(blk, sizeof blk, portMAX_DELAY);
            bi = 0;
            if (bn <= 0) {
                continue;
            }
        }
        c = blk[bi++];
#endif
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            if (overflow) {
                ESP_LOGW(TAG, "command too long");
            } else if (n) {
                run(line);
            }
#if CONFIG_TFN_DEVICE_PROTECTION
            memset(line, 0, sizeof line);
#endif
            n = 0;
            overflow = false;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = (char)c;
        } else {
            overflow = true;
        }
    }
}

void console_init(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    // the Watcher: UART0 through its USB-UART chip
    if (uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0) != ESP_OK) {
        return;
    }
    uart_vfs_dev_use_driver(UART_NUM_0);
#else
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
#if CONFIG_TFN_DEVICE_PROTECTION
    // A maintenance line carries up to 1024 hex characters. The driver's
    // 256-byte default can drop input while the console parses or checks it.
    cfg.rx_buffer_size = 4096;
    cfg.tx_buffer_size = 2048;
#else
    cfg.rx_buffer_size = 1024;  // (a line of sdusb put: about 600 characters at once)
#endif
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        return;
    }
    usb_serial_jtag_vfs_use_driver();  // the log keeps working through the driver
#endif
#if CONFIG_TFN_DEVICE_PROTECTION
    xTaskCreatePinnedToCore(console_task, "console", 12288, NULL, 2, NULL, 0);
#else
    // "pad" starts BLE here. The stack is 8192 bytes: with the line buffer on the stack, the BLE start overflows 6144
    // and the board restarts
    xTaskCreatePinnedToCore(console_task, "console", 8192, NULL, 2, NULL, 0);
#endif
}
