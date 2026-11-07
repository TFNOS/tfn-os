// The steps of one firmware update over the network (see ota_flow.h). Pure C: the host unit test compiles this file
// as it is (tests/unit/test_ota_flow.c).
#include "ota_flow.h"
#include <stdio.h>
#include <string.h>

void ota_flow_scan_tags(const char *tag, const char *buf, int n, bool *own, char other[16])
{
    int tl = (int)strlen(tag);
    for (int i = 0; i + OTA_FLOW_TAG_PFX < n; i++) {
        if (buf[i] != '@' || memcmp(buf + i, tag, OTA_FLOW_TAG_PFX)) {
            continue;
        }
        if (i + tl <= n && !memcmp(buf + i, tag, tl)) {
            *own = true;
            continue;
        }
        // another kind: the letters up to the closing '@' (all of it must be in this block)
        int k = 0;
        char t[16];
        for (int j = i + OTA_FLOW_TAG_PFX; j < n && k < 15; j++) {
            char c = buf[j];
            if (c == '@') {
                t[k] = 0;
                if (k) {
                    snprintf(other, 16, "%s", t);
                }
                break;
            }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                break;
            }
            t[k++] = c;
        }
    }
}

// The state of the body loop
typedef struct {
    const ota_flow_io_t *io;
    void *ctx;
    const ota_flow_cfg_t *cfg;
    ota_flow_mem_t *m;
    int total;
    bool hashing;  // the hash context runs
    bool hash_ok;  // no hash fault until now
} flow_t;

// the bytes p[0, len) at the file offset off: the image part goes into the hash, the last OTA_SIG_TRAILER bytes of
// the file go into the trailer copy
static void take(flow_t *f, int off, const char *p, int len)
{
    int limit = f->total - OTA_SIG_TRAILER;
    int h = off >= limit ? 0 : (off + len <= limit ? len : limit - off);
    if (h > 0 && f->hashing && !f->io->hash_update(f->ctx, (const uint8_t *)p, h)) {
        f->hash_ok = false;
    }
    if (len > h) {
        memcpy(f->m->trailer + (off + h - limit), p + h, (size_t)(len - h));
    }
}

static ota_flow_res_t first_block(flow_t *f, ota_flow_out_t *o, int *got)
{
    char *buf = f->m->buf;
    while (*got < 0x60) {
        int want = OTA_FLOW_BLOCK - *got < f->total - *got ? OTA_FLOW_BLOCK - *got : f->total - *got;
        int r = f->io->recv(f->ctx, buf + *got, want);
        if (r <= 0) {
            o->recv = r;
            o->done = *got;
            return OTA_FLOW_RECV;
        }
        *got += r;
    }
    if ((uint8_t)buf[0] != 0xE9) {
        return OTA_FLOW_IMAGE;
    }
    snprintf(o->ver, sizeof o->ver, "%.32s", buf + OTA_FLOW_DESC_VER);
    return OTA_FLOW_OK;
}

// the write of each block, the tag search, the hash and the trailer copy (n: the bytes of the first block)
static ota_flow_res_t blocks(flow_t *f, ota_flow_out_t *o, int n)
{
    char *buf = f->m->buf;
    bool own = false;
    int keep = 0, done = 0;
    for (;;) {
        int e = f->io->write(f->ctx, buf + keep, n - keep);
        if (e) {
            o->err = e;
            o->done = done;
            return OTA_FLOW_WRITE;
        }
        if (f->cfg->check) {
            take(f, done, buf + keep, n - keep);
        }
        done += n - keep;
        ota_flow_scan_tags(f->cfg->tag, buf, n, &own, o->other);
        f->io->step(f->ctx, OTA_STEP_PROGRESS, done, f->total);
        if (done >= f->total) {
            break;
        }
        keep = n < OTA_FLOW_KEEP ? n : OTA_FLOW_KEEP;
        memmove(buf, buf + n - keep, keep);
        int want = f->total - done < OTA_FLOW_BLOCK ? f->total - done : OTA_FLOW_BLOCK;
        int r = f->io->recv(f->ctx, buf + keep, want);
        if (r <= 0) {
            o->recv = r;
            o->done = done;
            return OTA_FLOW_RECV;
        }
        n = keep + r;
    }
    o->done = done;
    return own ? OTA_FLOW_OK : OTA_FLOW_TAG;
}

// the signature check of the full file (REPORT: the result only; ENFORCE: a fault stops the update)
static ota_flow_res_t check(flow_t *f, ota_flow_out_t *o)
{
    uint8_t hash[32];
    bool hok = f->hashing && f->io->hash_finish(f->ctx, hash) && f->hash_ok;
    f->hashing = false;
    ota_sig_want_t w = f->cfg->want;
    w.version = o->ver;
    f->cfg->check(f->m->trailer, OTA_SIG_TRAILER, (uint32_t)f->total, hok ? hash : NULL, &w, f->cfg->crypto, &o->sig);
    o->checked = true;
    bool acc = ota_sig_accept(f->cfg->mode, o->sig.res);
    f->io->on_sig(f->ctx, &o->sig, acc);
    return acc ? OTA_FLOW_OK : OTA_FLOW_SIG;
}

// the steps after the claim and the work memory
static ota_flow_res_t body(flow_t *f, ota_flow_out_t *o)
{
    int got = 0;
    ota_flow_res_t res = first_block(f, o, &got);
    if (res != OTA_FLOW_OK) {
        return res;
    }
    f->io->step(f->ctx, OTA_STEP_ERASE, 0, f->total);
    int e = f->io->begin(f->ctx, f->total);
    if (e) {
        o->err = e;
        return OTA_FLOW_BEGIN;
    }
    f->io->step(f->ctx, OTA_STEP_WRITE, 0, f->total);
    if (f->cfg->check) {
        f->hashing = f->io->hash_start(f->ctx);
        f->hash_ok = true;
    }
    res = blocks(f, o, got);
    if (res == OTA_FLOW_OK && f->cfg->check) {
        res = check(f, o);
    }
    if (res != OTA_FLOW_OK) {
        if (f->hashing) {
            f->io->hash_abort(f->ctx);
        }
        f->io->abort(f->ctx);
        return res;
    }
    e = f->io->end(f->ctx);
    if (e) {
        o->err = e;
        return OTA_FLOW_END;
    }
    e = f->io->set_boot(f->ctx);
    if (e) {
        o->err = e;
        return OTA_FLOW_BOOT;
    }
    return OTA_FLOW_OK;
}

ota_flow_res_t ota_flow_run(const ota_flow_io_t *io, void *ctx, const ota_flow_cfg_t *cfg, int total,
                            ota_flow_out_t *out)
{
    memset(out, 0, sizeof *out);
    out->total = total;
    out->sig.key = -1;
    if (cfg->slot_size <= 0) {
        return out->res = OTA_FLOW_SLOT;
    }
    if (io->busy(ctx)) {
        return out->res = OTA_FLOW_BUSY;
    }
    if (total < OTA_FLOW_MIN || total > cfg->slot_size) {
        return out->res = OTA_FLOW_SIZE;
    }
    if (!io->claim(ctx)) {
        return out->res = OTA_FLOW_CLAIM;
    }
    // (the end check needs internal RAM for a hash context; with too little, the update fails only after the erase
    // and the full send. So the same trial runs before the erase.)
    if (!io->trial(ctx)) {
        io->release(ctx);
        return out->res = OTA_FLOW_RAM;
    }
    io->wake(ctx);
    flow_t f = {.io = io, .ctx = ctx, .cfg = cfg, .total = total};
    f.m = io->alloc(ctx);
    if (!f.m) {
        io->release(ctx);
        return out->res = OTA_FLOW_MEM;
    }
    out->res = body(&f, out);
    io->free_mem(ctx, f.m);
    if (out->res != OTA_FLOW_OK) {
        io->release(ctx);
    }
    return out->res;
}
