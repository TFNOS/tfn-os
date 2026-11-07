// SD card requests over the existing serial console. Each reply is one CRC-checked frame. The reads (ls, stat, get)
// keep no open file or directory between requests; the put (below) keeps one file open between its chunks.
#include <limits.h>
#include <sys/stat.h>
#ifndef USBFS_ROOT
#define USBFS_ROOT BOARD_SD_ROOT
#endif
#ifndef USBFS_MOUNT
#define USBFS_MOUNT() board_sd_mount()
#endif
#ifndef USBFS_YIELD
#define USBFS_YIELD() vTaskDelay(1)
#endif
#define USBFS_PATH_MAX 240
#define USBFS_CHUNK    512

#ifndef USBFS_CRC
static uint32_t usbfs_crc(uint32_t crc, const uint8_t *data, size_t size)
{
    crc = ~crc;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
#define USBFS_CRC usbfs_crc
#endif
static int usbfs_digit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}
static bool usbfs_path(const char *hex, char full[sizeof USBFS_ROOT + USBFS_PATH_MAX])
{
    if (!hex) {
        return false;
    }
    size_t len = strlen(hex);
    if (!len || (len & 1) || len / 2 > USBFS_PATH_MAX - 1) {
        return false;
    }
    char *path = full + sizeof USBFS_ROOT - 1;
    memcpy(full, USBFS_ROOT, sizeof USBFS_ROOT - 1);
    for (size_t i = 0; i < len; i += 2) {
        int hi = usbfs_digit(hex[i]), lo = usbfs_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        unsigned ch = (unsigned)((hi << 4) | lo);
        if (ch < 32 || ch == 127 || ch == '\\' || ch == ':') {
            return false;
        }
        path[i / 2] = (char)ch;
    }
    len /= 2;
    path[len] = 0;
    if (path[0] != '/') {
        return false;
    }
    for (size_t start = 1; start < len;) {
        size_t end = start;
        while (end < len && path[end] != '/') {
            end++;
        }
        if (end == start || (end - start == 1 && path[start] == '.') ||
            (end - start == 2 && path[start] == '.' && path[start + 1] == '.')) {
            return false;
        }
        start = end + 1;
    }
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = 0;
    }
    return true;
}
static void usbfs_u64(uint8_t *out, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}
static void usbfs_u32(uint8_t *out, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}
static void usbfs_reply(uint32_t id, const uint8_t *payload, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    static char line[64 + 2 * (USBFS_CHUNK + 9)];  // (static: only the console task sends; not on its 6 KB stack)
    size_t pos = (size_t)snprintf(line, sizeof line, "USBFS1 %08lx ", (unsigned long)id);
    for (size_t i = 0; i < size; i++) {
        line[pos++] = digits[payload[i] >> 4];
        line[pos++] = digits[payload[i] & 15];
    }
    snprintf(line + pos, sizeof line - pos, " %08lx\n", (unsigned long)USBFS_CRC(0, payload, size));
    fputs(line, stdout);
    fflush(stdout);
}
static void usbfs_error(uint32_t id, const char *error)
{
    uint8_t payload[64];
    payload[0] = '!';
    size_t len = strlen(error);
    if (len > sizeof payload - 1) {
        len = sizeof payload - 1;
    }
    memcpy(payload + 1, error, len);
    usbfs_reply(id, payload, len + 1);
}
static bool usbfs_number(const char *text, int base, uint64_t *value)
{
    if (!text || !*text || text[0] == '-' || text[0] == '+') {
        return false;
    }
    uint64_t result = 0;
    for (const char *p = text; *p; p++) {
        int digit = usbfs_digit(*p);
        if (digit < 0 || digit >= base || result > (UINT64_MAX - (unsigned)digit) / (unsigned)base) {
            return false;
        }
        result = result * (unsigned)base + (unsigned)digit;
    }
    *value = result;
    return true;
}
// THE PUT (a board with no WiFi gets its apps and data over USB):
// "put <hex path> <offset> <base64 of 1..USBFS_CHUNK bytes>" writes at the offset; offset 0 makes the file new (and its
// folders). One file stays open between the chunks (an open for each chunk is slow on FAT); a new path, another
// offset or "close" closes it.
static FILE *s_put_f;
static char s_put_path[sizeof USBFS_ROOT + USBFS_PATH_MAX];
static uint64_t s_put_off;
static void usbfs_put_close(void)
{
    if (s_put_f) {
        fclose(s_put_f);
    }
    s_put_f = NULL;
    s_put_path[0] = 0;
}
static bool usbfs_put_open(const char *path, uint64_t offset)
{
    if (s_put_f && !strcmp(path, s_put_path) && offset == s_put_off) {
        return true;
    }
    usbfs_put_close();
    if (!offset) {
        char dir[sizeof s_put_path];
        snprintf(dir, sizeof dir, "%s", path);
        for (char *p = dir + sizeof USBFS_ROOT; *p; p++) {
            if (*p == '/') {
                *p = 0;
                mkdir(dir, 0777);
                *p = '/';
            }
        }
        s_put_f = fopen(path, "wb");
    } else {
        s_put_f = fopen(path, "r+b");
        if (s_put_f && fseek(s_put_f, (long)offset, SEEK_SET)) {
            usbfs_put_close();
            return false;
        }
    }
    if (!s_put_f) {
        return false;
    }
    snprintf(s_put_path, sizeof s_put_path, "%s", path);
    s_put_off = offset;
    return true;
}
static int usbfs_b64(const char *s, uint8_t *out, int max)
{
    int n = 0, bits = 0;
    uint32_t acc = 0;
    for (; *s && *s != '='; s++) {
        const char c = *s;
        const int v = c >= 'A' && c <= 'Z'   ? c - 'A'
                      : c >= 'a' && c <= 'z' ? c - 'a' + 26
                      : c >= '0' && c <= '9' ? c - '0' + 52
                      : c == '+'             ? 62
                      : c == '/'             ? 63
                                             : -1;
        if (v < 0) {
            return -1;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= max) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    return n;
}
// put <hex path> <offset> <base64>: write one chunk at the offset of the file
static void usbfs_put_cmd(uint32_t id, const char *hex, const char *first, const char *second, uint8_t *payload)
{
    uint64_t off = 0;
    char full[sizeof USBFS_ROOT + USBFS_PATH_MAX];
    if (!usbfs_path(hex, full) || !usbfs_number(first, 10, &off) || !second || off > LONG_MAX) {
        usbfs_error(id, "BAD_REQUEST");
        return;
    }
    if (!USBFS_MOUNT()) {
        usbfs_error(id, "NO_CARD");
        return;
    }
    const int n = usbfs_b64(second, payload, USBFS_CHUNK);
    if (n <= 0) {
        usbfs_error(id, "BAD_DATA");
        return;
    }
    if (!usbfs_put_open(full, off)) {
        usbfs_error(id, "OPEN_FAILED");
        return;
    }
    if (fwrite(payload, 1, (size_t)n, s_put_f) != (size_t)n) {
        usbfs_put_close();
        usbfs_error(id, "WRITE_FAILED");
        return;
    }
    s_put_off += (uint64_t)n;
    payload[0] = 'W';
    usbfs_u64(payload + 1, s_put_off);
    usbfs_reply(id, payload, 9);
}

// ls: the entry number offset of the folder path (or the end mark)
static void usbfs_list(uint32_t id, const char *path, uint64_t offset, uint8_t *payload)
{
    DIR *dir = opendir(path);
    if (!dir) {
        usbfs_error(id, "NOT_DIRECTORY");
        return;
    }
    struct dirent *entry;
    uint64_t index = 0;
    bool found = false;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        if (index++ != offset) {
            if (!(index % 32)) {
                USBFS_YIELD();
            }
            continue;
        }
        size_t len = strlen(entry->d_name);
        char file[sizeof USBFS_ROOT + USBFS_PATH_MAX + 256];  // (the size of the path + 256)
        struct stat st;
        snprintf(file, sizeof file, "%s/%s", path, entry->d_name);
        if (len > 255 || stat(file, &st) || st.st_size < 0) {
            closedir(dir);
            usbfs_error(id, "STAT_FAILED");
            return;
        }
        payload[0] = 'L';
        payload[1] = S_ISDIR(st.st_mode) ? 'D' : 'F';
        usbfs_u64(payload + 2, (uint64_t)st.st_size);
        memcpy(payload + 10, entry->d_name, len);
        usbfs_reply(id, payload, 10 + len);
        found = true;
        break;
    }
    closedir(dir);
    if (!found) {
        payload[0] = 'E';
        usbfs_reply(id, payload, 1);
    }
}

// stat: the size and the CRC of the open file (it closes the file)
static void usbfs_stat(uint32_t id, FILE *file, const struct stat *st, uint8_t *payload)
{
    uint8_t block[USBFS_CHUNK];
    uint32_t crc = 0;
    uint64_t total = 0;
    size_t n;
    unsigned blocks = 0;
    while ((n = fread(block, 1, sizeof block, file))) {
        crc = USBFS_CRC(crc, block, n);
        total += n;
        blocks++;
        if (!(blocks % 32)) {
            USBFS_YIELD();
        }
        if (!(blocks % 512)) {
            const uint8_t busy = 'B';
            usbfs_reply(id, &busy, 1);
        }
    }
    bool ok = !ferror(file) && total == (uint64_t)st->st_size;
    fclose(file);
    if (!ok) {
        usbfs_error(id, "FILE_CHANGED_OR_READ_FAILED");
        return;
    }
    payload[0] = 'S';
    usbfs_u64(payload + 1, total);
    usbfs_u32(payload + 9, crc);
    usbfs_reply(id, payload, 13);
}

// (noinline: the buffers of a request are on the stack only during sdusb, not in the frame of each console command)
static __attribute__((noinline)) void usbfs_command(char *args)
{
    uint64_t parsed = 0;
    char *idtext = args ? strtok(args, " \t") : NULL;
    if (!usbfs_number(idtext, 16, &parsed) || parsed > UINT32_MAX) {
        usbfs_error(0, "BAD_ID");
        return;
    }
    uint32_t id = (uint32_t)parsed;
    char *op = strtok(NULL, " \t"), *hex = strtok(NULL, " \t"), *first = strtok(NULL, " \t"),
         *second = strtok(NULL, " \t");
    char *extra = strtok(NULL, " \t");
    if (!op || extra) {
        usbfs_error(id, "BAD_REQUEST");
        return;
    }
    uint8_t payload[USBFS_CHUNK + 9];
    if (!strcmp(op, "ping") && !hex) {
        payload[0] = 'P';
        payload[1] = USBFS_CHUNK & 255;
        payload[2] = USBFS_CHUNK >> 8;
        usbfs_reply(id, payload, 3);
        return;
    }
    if (!strcmp(op, "close") && !hex) {
        usbfs_put_close();
        payload[0] = 'C';
        usbfs_reply(id, payload, 1);
        return;
    }
    if (!strcmp(op, "put")) {
        usbfs_put_cmd(id, hex, first, second, payload);
        return;
    }
    bool list = !strcmp(op, "ls"), info = !strcmp(op, "stat"), get = !strcmp(op, "get");
    uint64_t offset = 0, length = 0;
    if ((!list && !info && !get) || (info && first) || ((list || get) && !usbfs_number(first, 10, &offset)) ||
        (list && second) || (get && (!usbfs_number(second, 10, &length) || !length || length > USBFS_CHUNK))) {
        usbfs_error(id, "BAD_REQUEST");
        return;
    }
    char path[sizeof USBFS_ROOT + USBFS_PATH_MAX];
    if (!usbfs_path(hex, path)) {
        usbfs_error(id, "BAD_PATH");
        return;
    }
    if (!USBFS_MOUNT()) {
        usbfs_error(id, "NO_CARD");
        return;
    }
    if (list) {
        usbfs_list(id, path, offset, payload);
        return;
    }
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        usbfs_error(id, "NOT_FILE");
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        usbfs_error(id, "OPEN_FAILED");
        return;
    }
    if (info) {
        usbfs_stat(id, file, &st, payload);
        return;
    }
    if (offset > (uint64_t)st.st_size || offset > LONG_MAX || length > (uint64_t)st.st_size - offset ||
        fseek(file, (long)offset, SEEK_SET)) {
        fclose(file);
        usbfs_error(id, "RANGE");
        return;
    }
    payload[0] = 'R';
    usbfs_u64(payload + 1, offset);
    size_t n = fread(payload + 9, 1, (size_t)length, file);
    bool ok = n == length && !ferror(file);
    fclose(file);
    if (!ok) {
        usbfs_error(id, "FILE_CHANGED_OR_READ_FAILED");
        return;
    }
    usbfs_reply(id, payload, n + 9);
}
