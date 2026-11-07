// FILES: the job (copy, delete, format), a part of it in each frame (main/app_files.c has the rest).
#include "app_files_priv.h"

static uint8_t s_buf[CHUNK];

// ---------------------------------------------------------------- the job (copy, delete, format)

static bool job_add(const char *rel, const char *base)
{
    if (fil_job.n >= JOB_MAX) {
        return false;
    }
    if (!fil_join(fil_p1, sizeof fil_p1, base, rel, NULL)) {
        return false;
    }
    struct stat st;
    if (stat(fil_p1, &st) != 0) {
        return false;
    }
    jit_t *e = &fil_job.it[fil_job.n++];
    snprintf(e->rel, sizeof e->rel, "%s", rel);
    e->dir = S_ISDIR(st.st_mode);
    e->size = e->dir ? 0 : (uint32_t)st.st_size;
    fil_job.total += e->size;
    if (!e->dir) {
        fil_job.files++;
    }
    return true;
}

// all files and folders under base (base itself first), parents before their contents; false: too many, or an error
bool fil_job_scan(const char *base)
{
    fil_job.n = fil_job.files = 0;
    fil_job.total = 0;
    if (!job_add("", base)) {
        return false;
    }
    for (int k = 0; k < fil_job.n; k++) {
        if (!fil_job.it[k].dir) {
            continue;
        }
        if (!fil_join(fil_p2, sizeof fil_p2, base, fil_job.it[k].rel, NULL)) {
            return false;
        }
        DIR *d = opendir(fil_p2);
        if (!d) {
            return false;
        }
        struct dirent *de;
        bool ok = true;
        while (ok && (de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }
            char r[JOB_REL];
            ok = snprintf(r, sizeof r, "%s/%s", fil_job.it[k].rel, de->d_name) < (int)sizeof r && job_add(r, base);
        }
        closedir(d);
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool fil_job_alloc(void)
{
    if (!fil_job.it) {
        fil_job.it = heap_caps_malloc(JOB_MAX * sizeof(jit_t), MALLOC_CAP_SPIRAM);
    }
    return fil_job.it != NULL;
}

void fil_job_free(void)
{
    heap_caps_free(fil_job.it);
    fil_job.it = NULL;
}

// remove a tree at once (the small folder of an install)
void fil_rm_tree_now(const char *base)
{
    if (!fil_exists(base)) {
        return;
    }
    if (!fil_job_alloc() || !fil_job_scan(base)) {
        return;
    }
    for (int i = fil_job.n - 1; i >= 0; i--) {
        if (!fil_join(fil_p1, sizeof fil_p1, base, fil_job.it[i].rel, NULL)) {
            continue;
        }
        if (fil_job.it[i].dir) {
            rmdir(fil_p1);
        } else {
            unlink(fil_p1);
        }
    }
    fil_job.n = 0;
}

void fil_job_close(bool drop_cur)
{
    if (fil_job.fi) {
        fclose(fil_job.fi);
    }
    if (fil_job.fo) {
        fclose(fil_job.fo);
    }
    if (fil_job.fo && drop_cur) {
        unlink(fil_job.cur);  // (a part of a file: it goes)
    }
    fil_job.fi = fil_job.fo = NULL;
}

static void job_after(void)
{
    fil_job.kind = J_NONE;
    fil_job_free();
    fil_space_read();
    fil_list_dirty = true;
    fil_page_list();
}

static void job_fail(const char *why)
{
    fil_job_close(true);
    bool inst = fil_job.install;
    fil_say(why, true, tfn_now_ms());
    if (inst) {
        fil_rm_tree_now(fil_job.dst);
    }
    job_after();
}

static void job_done(void)
{
    fil_job_close(false);
    char m[112];
    if (fil_job.kind == J_COPY && fil_job.install) {
        char msg[96];
        bool ok = tfn_install(fil_job.dst, fil_job.place, msg, sizeof msg);
        if (!ok) {
            if (!msg[0]) {
                snprintf(msg, sizeof msg, "the app did not install");
            }
            fil_rm_tree_now(fil_job.dst);
        }
        fil_upper(msg);
        fil_job.kind = J_NONE;
        fil_say(msg, !ok, tfn_now_ms());
        job_after();
        return;
    }
    if (fil_job.kind == J_COPY) {
        snprintf(m, sizeof m, "COPIED TO THE %s", fil_root_name(fil_job.dst_root));
    } else {
        snprintf(m, sizeof m, "DELETED");
    }
    fil_say(m, false, tfn_now_ms());
    sfx_play(fil_job.kind == J_DELETE ? SFX_CUT : SFX_COIN);
    job_after();
}

// start a copy of the tree src to dst (dst is not there, or the mode is REPLACE)
void fil_job_copy(const char *src, const char *dst, const char *title, int dst_root, bool install, int place)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (!device_guard_local_path(src) || !device_guard_local_path(dst)) {
        fil_say("UNLOCK ADMIN ACCESS IN SETUP", true, tfn_now_ms());
        return;
    }
#endif
    snprintf(fil_job.src, sizeof fil_job.src, "%s", src);
    snprintf(fil_job.dst, sizeof fil_job.dst, "%s", dst);
    snprintf(fil_job.title, sizeof fil_job.title, "%s", title);
    fil_job.kind = J_COPY;
    fil_job.i = fil_job.files_done = 0;
    fil_job.done = 0;
    fil_job.install = install;
    fil_job.place = place;
    fil_job.dst_root = dst_root;
    fil_page = P_JOB;
    fil_hot = -1;
}

void fil_job_step(void)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (fil_job.kind == J_COPY && (!device_guard_local_path(fil_job.src) || !device_guard_local_path(fil_job.dst))) {
        job_fail("ADMIN ACCESS EXPIRED");
        return;
    }
#endif
    if (fil_job.kind == J_FORMAT) {
        bool ok = board_sd_format();
        tfn_rescan();  // (the apps of the card are gone: the menu takes them out)
        fil_job.kind = J_NONE;
        fil_card_look();
        fil_space_read();
        if (ok) {
            fil_say("THE CARD IS FORMATTED", false, tfn_now_ms());
        } else {
            fil_say("FORMAT IS NOT IN THIS FIRMWARE, OR IT FAILED", true, tfn_now_ms());
        }
        fil_go_home();
        return;
    }
    for (int ops = 0; ops < 8 && fil_job.kind != J_NONE; ops++) {
        if (fil_job.fi) {
            size_t r = fread(s_buf, 1, CHUNK, fil_job.fi);
            if (r > 0 && fwrite(s_buf, 1, r, fil_job.fo) != r) {
                job_fail(fil_job.dst_root == RT_CARD ? "THE CARD IS FULL, OR IT FAILED"
                                                     : "THE BOARD IS FULL, OR IT FAILED");
                return;
            }
            fil_job.done += r;
            if (r < CHUNK) {
                if (ferror(fil_job.fi)) {
                    job_fail("A FILE DID NOT READ");
                    return;
                }
                if (fclose(fil_job.fo) != 0) {
                    fil_job.fo = NULL;
                    unlink(fil_job.cur);
                    job_fail("A FILE DID NOT WRITE (FULL?)");
                    return;
                }
                fil_job.fo = NULL;
                fclose(fil_job.fi);
                fil_job.fi = NULL;
                fil_job.i++;
                fil_job.files_done++;
            }
            return;  // (one part of a file in each frame)
        }
        if (fil_job.i >= fil_job.n) {
            job_done();
            return;
        }
        if (fil_job.kind == J_COPY) {
            const jit_t *e = &fil_job.it[fil_job.i];
            if (!fil_join(fil_p1, sizeof fil_p1, fil_job.src, e->rel, NULL) ||
                !fil_join(fil_job.cur, sizeof fil_job.cur, fil_job.dst, e->rel, NULL)) {
                job_fail("THE PATH IS TOO LONG");
                return;
            }
            const char *sl = strrchr(e->rel, '/');
            snprintf(fil_job.name, sizeof fil_job.name, "%.95s", sl ? sl + 1 : e->rel);
            if (!e->rel[0]) {  // (the item itself: the last part of its path)
                const char *bs = strrchr(fil_job.src, '/');
                snprintf(fil_job.name, sizeof fil_job.name, "%.95s", bs ? bs + 1 : fil_job.src);
            }
            if (e->dir) {
                if (MKDIR(fil_job.cur) != 0 && !fil_is_dir(fil_job.cur)) {
                    job_fail("A FOLDER DID NOT MAKE");
                    return;
                }
                fil_job.i++;
                continue;
            }
            fil_job.fi = fopen(fil_p1, "rb");
            fil_job.fo = fil_job.fi ? fopen(fil_job.cur, "wb") : NULL;
            if (!fil_job.fi || !fil_job.fo) {
                if (fil_job.fi) {
                    fclose(fil_job.fi);
                }
                fil_job.fi = NULL;
                fil_job.fo = NULL;
                job_fail("A FILE DID NOT OPEN");
                return;
            }
        } else {
            // delete: the contents first (the list from the end)
            const jit_t *e = &fil_job.it[fil_job.n - 1 - fil_job.i];
            if (!fil_join(fil_p1, sizeof fil_p1, fil_job.src, e->rel, NULL)) {
                job_fail("THE PATH IS TOO LONG");
                return;
            }
            const char *sl = strrchr(e->rel, '/');
            snprintf(fil_job.name, sizeof fil_job.name, "%.95s", sl ? sl + 1 : e->rel);
            if (!e->rel[0]) {  // (the item itself: the last part of its path)
                const char *bs = strrchr(fil_job.src, '/');
                snprintf(fil_job.name, sizeof fil_job.name, "%.95s", bs ? bs + 1 : fil_job.src);
            }
            if ((e->dir ? rmdir(fil_p1) : unlink(fil_p1)) != 0) {
                job_fail("A FILE DID NOT DELETE");
                return;
            }
            if (!e->dir) {
                fil_job.files_done++;
            }
            fil_job.i++;
        }
    }
}

void fil_job_stop(int64_t now)
{
    if (fil_job.kind == J_NONE || fil_job.kind == J_FORMAT) {
        return;
    }
    fil_job_close(true);
    bool inst = fil_job.install;
    char m[80];
    snprintf(m, sizeof m, "STOPPED: %d OF %d FILES DONE", fil_job.files_done, fil_job.files);
    fil_say(m, true, now);
    if (inst) {
        fil_rm_tree_now(fil_job.dst);
    }
    job_after();
}
