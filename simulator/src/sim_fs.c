// The simulator: the file systems of the board in the data folder of the PC. An absolute path of the board goes under
// the root folder: "/data/chess.bin" -> <root>/data/chess.bin, "/sdcard/apps" -> <root>/sdcard/apps. The paths are
// UTF-8 (sim_os.h). The FAT partition /data reports the size of the slim layout.
#define SIM_FS_IMPL
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sim.h"
#include "sim_os.h"
// (build.py gives sim_force.h only to the files of the firmware: here the calls are the real ones of the PC)
#undef fopen
#undef opendir
#undef stat
#undef mkdir
#undef rmdir
#undef remove
#undef unlink
#undef rename

#define SIM_PATH 1024
#define DATA_PART_BYTES 0x8E0000ull     // the storage partition of partitions_slim.csv
static char s_root[SIM_PATH];
static volatile bool s_sd = true;

static void mkdirs(const char *dir)
{
    char p[SIM_PATH];
    snprintf(p, sizeof p, "%s", dir);
    // (from the second character: "/home" and "C:/" are not made)
    for (char *c = p + 1; *c; c++) {
        if (*c == '/' || *c == '\\') {
            char k = *c;
            *c = 0;
            sim_os_mkdir(p);
            *c = k;
        }
    }
    sim_os_mkdir(p);
}

void sim_fs_init(const char *root)
{
    snprintf(s_root, sizeof s_root, "%s", root);
    for (char *c = s_root; *c; c++) if (*c == '\\') *c = '/';
    size_t n = strlen(s_root);
    if (n && s_root[n - 1] == '/') s_root[n - 1] = 0;
    char p[SIM_PATH];
    snprintf(p, sizeof p, "%s/data", s_root);
    mkdirs(p);
    snprintf(p, sizeof p, "%s/sdcard", s_root);
    mkdirs(p);
}

const char *sim_fs_root(void) { return s_root; }
void sim_fs_sd_present(bool on) { s_sd = on; }
bool sim_fs_sd_is_present(void) { return s_sd; }

bool sim_fs_map(const char *path, char *out, int len)
{
    if (!path) return false;
    if (path[0] != '/' || path[1] == '/' || !s_root[0]) {
        snprintf(out, (size_t)len, "%s", path);      // (a relative path, or a path of the PC)
        return true;
    }
    if (!strncmp(path, "/sdcard", 7) && (path[7] == 0 || path[7] == '/') && !s_sd) return false;   // (no card)
    snprintf(out, (size_t)len, "%s%s", s_root, path);
    return true;
}

FILE *sim_fopen(const char *path, const char *mode)
{
    char m[SIM_PATH];
    return sim_fs_map(path, m, sizeof m) ? sim_os_fopen(m, mode) : NULL;
}

DIR *sim_opendir(const char *path)
{
    char m[SIM_PATH];
    if (!sim_fs_map(path, m, sizeof m)) {
        return NULL;
    }
    return opendir(m);
}

int sim_stat(const char *path, struct stat *st)
{
    char m[SIM_PATH];
    return sim_fs_map(path, m, sizeof m) ? sim_os_stat(m, st) : -1;
}

int sim_mkdir(const char *path)
{
    char m[SIM_PATH];
    return sim_fs_map(path, m, sizeof m) ? sim_os_mkdir(m) : -1;
}

int sim_rmdir(const char *path)
{
    char m[SIM_PATH];
    return sim_fs_map(path, m, sizeof m) ? sim_os_rmdir(m) : -1;
}

int sim_unlink(const char *path)
{
    char m[SIM_PATH];
    return sim_fs_map(path, m, sizeof m) ? sim_os_unlink(m) : -1;
}

int sim_remove(const char *path)
{
    char m[SIM_PATH];
    if (!sim_fs_map(path, m, sizeof m)) {
        return -1;
    }
    return sim_os_is_dir(m) ? sim_os_rmdir(m) : sim_os_unlink(m);
}

int sim_rename(const char *from, const char *to)
{
    char a[SIM_PATH];
    char b[SIM_PATH];
    if (!sim_fs_map(from, a, sizeof a) || !sim_fs_map(to, b, sizeof b)) {
        return -1;
    }
    return sim_os_rename(a, b, false);          // (FAT: a rename onto a file that exists fails)
}

// ---------------------------------------------------------------- the FAT partition
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char *base, const char *label, const esp_vfs_fat_mount_config_t *cfg,
                                           wl_handle_t *wl)
{
    (void)label; (void)cfg;
    char m[SIM_PATH];
    if (!sim_fs_map(base, m, sizeof m)) return ESP_FAIL;
    mkdirs(m);
    if (wl) *wl = 1;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_spiflash_unmount_rw_wl(const char *base, wl_handle_t wl)
{
    (void)base; (void)wl;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_info(const char *base, uint64_t *total, uint64_t *free_bytes)
{
    // (a walk of the folder at most each 2 s: SETUP shows the free space in each frame)
    static uint64_t used;
    static int64_t at;
    if (!at || sim_now_us() - at > 2000000) {
        char m[SIM_PATH];
        if (!sim_fs_map(base, m, sizeof m)) {
            return ESP_FAIL;
        }
        used = sim_os_dir_bytes(m, 8);
        at = sim_now_us();
    }
    uint64_t t = DATA_PART_BYTES - 0x20000;             // (less the FAT and the wear levelling)
    if (total) *total = t;
    if (free_bytes) *free_bytes = used < t ? t - used : 0;
    return ESP_OK;
}
