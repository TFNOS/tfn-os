// Included only by the serial console. Every request has already passed owner
// authentication; readers receive the existing USBFS1 CRC-protected framing.
#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
#include "device_guard_core.h"
#include "esp_flash.h"
#include "ota.h"
static bool guard_hex_bytes(const char *hex, uint8_t *data, size_t cap, size_t *len)
{
    if (!hex) {
        return false;
    }
    size_t n = strlen(hex);
    if (!n || (n & 1) || n / 2 > cap) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2) {
        int hi = usbfs_digit(hex[i]), lo = usbfs_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        data[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *len = n / 2;
    return true;
}
static void guard_ack(uint32_t id, uint64_t value)
{
    uint8_t p[9] = {'A'};
    usbfs_u64(p + 1, value);
    usbfs_reply(id, p, sizeof p);
}
// ping, flash_info and flash_get: true when the request was one of them (the reply is sent)
static bool guard_flash_op(uint32_t id, const char *op, char *a, char *b, char *c, uint8_t *payload)
{
    uint64_t offset = 0, length = 0;
    if (!strcmp(op, "ping") && !a) {
        payload[0] = 'P';
        payload[1] = USBFS_CHUNK & 255;
        payload[2] = USBFS_CHUNK >> 8;
        usbfs_reply(id, payload, 3);
        return true;
    }
    if (!strcmp(op, "flash_info") && !a) {
        uint32_t size = 0;
        if (esp_flash_get_size(NULL, &size) != ESP_OK) {
            usbfs_error(id, "FLASH_FAILED");
            return true;
        }
        payload[0] = 'Z';
        usbfs_u64(payload + 1, size);
        usbfs_reply(id, payload, 9);
        return true;
    }
    if (!strcmp(op, "flash_get") && !c) {
        uint32_t size = 0;
        if (!usbfs_number(a, 10, &offset) || !usbfs_number(b, 10, &length) || !length || length > USBFS_CHUNK ||
            esp_flash_get_size(NULL, &size) != ESP_OK || offset > size || length > size - offset) {
            usbfs_error(id, "RANGE");
            return true;
        }
        if (esp_flash_read(NULL, payload + 9, (uint32_t)offset, (uint32_t)length) != ESP_OK) {
            usbfs_error(id, "READ_FAILED");
            return true;
        }
        payload[0] = 'R';
        usbfs_u64(payload + 1, offset);
        usbfs_reply(id, payload, (size_t)length + 9);
        return true;
    }
    return false;
}

// fw_begin, fw_write, fw_finish and fw_abort (the firmware by USB): true when the request was one of them
static bool guard_fw_op(uint32_t id, const char *op, char *a, char *b, char *c, uint8_t *payload, char *message,
                        size_t message_size)
{
    uint64_t offset = 0, length = 0;
    size_t bytes = 0;
    if (!strcmp(op, "fw_begin") && !c) {
        if (!usbfs_number(a, 10, &length) || length > SIZE_MAX || !guard_hex_bytes(b, payload, 32, &bytes) ||
            bytes != 32) {
            usbfs_error(id, "BAD_REQUEST");
            return true;
        }
        if (!ota_usb_begin((size_t)length, payload, message, message_size)) {
            usbfs_error(id, message);
            return true;
        }
        guard_ack(id, length);
        return true;
    }
    if (!strcmp(op, "fw_write") && !c) {
        if (!usbfs_number(a, 10, &offset) || offset > SIZE_MAX || !guard_hex_bytes(b, payload, USBFS_CHUNK, &bytes)) {
            usbfs_error(id, "BAD_REQUEST");
            return true;
        }
        if (!ota_usb_write((size_t)offset, payload, bytes, message, message_size)) {
            usbfs_error(id, message);
            return true;
        }
        guard_ack(id, offset + bytes);
        return true;
    }
    if (!strcmp(op, "fw_finish") && !a) {
        if (!ota_usb_finish(message, message_size)) {
            usbfs_error(id, message);
            return true;
        }
        guard_ack(id, 0);
        return true;
    }
    if (!strcmp(op, "fw_abort") && !a) {
        ota_usb_abort();
        guard_ack(id, 0);
        return true;
    }
    return false;
}

// install, mkdir, rm and rename of a maintenance path: true when the request was one of them
static bool guard_path_op(uint32_t id, const char *op, char *b, char *c, const char *path, char *other,
                          size_t other_size, char *message, size_t message_size)
{
    if (!strcmp(op, "install") && !b) {
        if (strncmp(path, "/data/owner-upload/", 19)) {
            usbfs_error(id, "BAD_INSTALL_PATH");
            return true;
        }
        if (!tfn_install(path, TFN_BOARD, message, message_size)) {
            usbfs_error(id, message);
            return true;
        }
        guard_ack(id, 0);
        return true;
    }
    if (!strcmp(op, "mkdir") && !b) {
        struct stat st;
        bool ok = !mkdir(path, 0777) || (!stat(path, &st) && S_ISDIR(st.st_mode));
        if (ok) {
            guard_ack(id, 0);
        } else {
            usbfs_error(id, "MKDIR_FAILED");
        }
        return true;
    }
    if (!strcmp(op, "rm") && !b) {
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode) || remove(path)) {
            usbfs_error(id, "REMOVE_FAILED");
        } else {
            guard_ack(id, 0);
        }
        return true;
    }
    if (!strcmp(op, "rename") && !c) {
        if (!dg_maintenance_path(b, other, other_size) || !strcmp(path, "/data") || !strcmp(other, "/data")) {
            usbfs_error(id, "BAD_PATH");
            return true;
        }
        if (rename(path, other)) {
            usbfs_error(id, "RENAME_FAILED");
        } else {
            guard_ack(id, 0);
        }
        return true;
    }
    return false;
}

// ls: the entry number offset of the folder path (or the end mark)
static void guard_list(uint32_t id, const char *path, uint64_t offset, uint8_t *payload)
{
    DIR *dir = opendir(path);
    if (!dir) {
        usbfs_error(id, "NOT_DIRECTORY");
        return;
    }
    struct dirent *e;
    uint64_t index = 0;
    bool found = false;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        if (index++ != offset) {
            continue;
        }
        char full[USBFS_PATH_MAX + 256];
        struct stat st;
        size_t n = strlen(e->d_name);
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (n > 255 || stat(full, &st) || st.st_size < 0) {
            closedir(dir);
            usbfs_error(id, "STAT_FAILED");
            return;
        }
        payload[0] = 'L';
        payload[1] = S_ISDIR(st.st_mode) ? 'D' : 'F';
        usbfs_u64(payload + 2, (uint64_t)st.st_size);
        memcpy(payload + 10, e->d_name, n);
        usbfs_reply(id, payload, 10 + n);
        found = true;
        break;
    }
    closedir(dir);
    if (!found) {
        payload[0] = 'E';
        usbfs_reply(id, payload, 1);
    }
}

// stat: the size and the CRC of the open file f (it closes f)
static void guard_stat(uint32_t id, FILE *f, const struct stat *st, uint8_t *payload)
{
    uint32_t crc = 0;
    uint64_t size = 0;
    size_t n;
    unsigned blocks = 0;
    while ((n = fread(payload, 1, USBFS_CHUNK, f))) {
        crc = USBFS_CRC(crc, payload, n);
        size += n;
        if (!(++blocks % 32)) {
            USBFS_YIELD();
        }
        if (!(blocks % 512)) {
            const uint8_t busy = 'B';
            usbfs_reply(id, &busy, 1);
        }
    }
    bool ok = !ferror(f) && size == (uint64_t)st->st_size;
    fclose(f);
    if (!ok) {
        usbfs_error(id, "READ_FAILED");
        return;
    }
    payload[0] = 'S';
    usbfs_u64(payload + 1, size);
    usbfs_u32(payload + 9, crc);
    usbfs_reply(id, payload, 13);
}

// put, ls, stat and get of a maintenance path
static void guard_file_io(uint32_t id, const char *op, char *b, char *c, const char *path, uint8_t *payload)
{
    uint64_t offset = 0, length = 0;
    size_t bytes = 0;
    bool put = !strcmp(op, "put"), get = !strcmp(op, "get"), list = !strcmp(op, "ls"), info = !strcmp(op, "stat");
    if (put || get || list) {
        if (!usbfs_number(b, 10, &offset) || offset > LONG_MAX) {
            usbfs_error(id, "BAD_OFFSET");
            return;
        }
    }
    if (put) {
        if (!guard_hex_bytes(c, payload, USBFS_CHUNK, &bytes)) {
            usbfs_error(id, "BAD_DATA");
            return;
        }
        FILE *f = fopen(path, offset ? "r+b" : "wb");
        bool ok = f && !fseek(f, (long)offset, SEEK_SET) && fwrite(payload, 1, bytes, f) == bytes;
        if (f && fclose(f)) {
            ok = false;
        }
        if (ok) {
            guard_ack(id, offset + bytes);
        } else {
            usbfs_error(id, "WRITE_FAILED");
        }
        return;
    }
    if (list && !c) {
        guard_list(id, path, offset, payload);
        return;
    }
    if ((!info && !get) || (info && b) || (get && (!usbfs_number(c, 10, &length) || !length || length > USBFS_CHUNK))) {
        usbfs_error(id, "BAD_REQUEST");
        return;
    }
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        usbfs_error(id, "NOT_FILE");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        usbfs_error(id, "OPEN_FAILED");
        return;
    }
    if (info) {
        guard_stat(id, f, &st, payload);
        return;
    }
    if (offset > (uint64_t)st.st_size || length > (uint64_t)st.st_size - offset || fseek(f, (long)offset, SEEK_SET)) {
        fclose(f);
        usbfs_error(id, "RANGE");
        return;
    }
    size_t n = fread(payload + 9, 1, (size_t)length, f);
    bool ok = n == length && !ferror(f);
    fclose(f);
    if (!ok) {
        usbfs_error(id, "READ_FAILED");
        return;
    }
    payload[0] = 'R';
    usbfs_u64(payload + 1, offset);
    usbfs_reply(id, payload, n + 9);
}

static __attribute__((noinline)) void guard_maintenance(char *args)
{
    char *it = args ? strtok(args, " \t") : NULL;
    uint64_t number;
    if (!usbfs_number(it, 16, &number) || number > UINT32_MAX) {
        usbfs_error(0, "BAD_ID");
        return;
    }
    uint32_t id = (uint32_t)number;
    char *op = strtok(NULL, " \t"), *a = strtok(NULL, " \t"), *b = strtok(NULL, " \t"), *c = strtok(NULL, " \t"),
         *extra = strtok(NULL, " \t");
    if (!device_guard_usb_admin()) {
        usbfs_error(id, "ADMIN_REQUIRED");
        return;
    }
    if (!op || extra) {
        usbfs_error(id, "BAD_REQUEST");
        return;
    }
    uint8_t payload[USBFS_CHUNK + 9];
    if (guard_flash_op(id, op, a, b, c, payload)) {
        return;
    }
    char message[64];
    if (guard_fw_op(id, op, a, b, c, payload, message, sizeof message)) {
        return;
    }
    char path[USBFS_PATH_MAX], other[USBFS_PATH_MAX];
    if (!dg_maintenance_path(a, path, sizeof path)) {
        usbfs_error(id, "BAD_PATH");
        return;
    }
    if (!strncmp(path, "/sdcard", 7) && !board_sd_mount()) {
        usbfs_error(id, "NO_CARD");
        return;
    }
    if (guard_path_op(id, op, b, c, path, other, sizeof other, message, sizeof message)) {
        return;
    }
    guard_file_io(id, op, b, c, path, payload);
}
#endif
