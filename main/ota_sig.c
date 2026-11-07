// The signature of a firmware update: the parse and the check of the trailer "TFNOTA1" (see ota_sig.h).
// Pure C: the host unit tests compile this file as it is (tests/unit/test_ota_sig.c).
#include "ota_sig.h"
#include <stdio.h>
#include <string.h>

#define OFF_FORMAT 8
#define OFF_LEN    12
#define OFF_HASH   16
#define OFF_KIND   48
#define OFF_VER    64
#define OFF_SV     96
#define OFF_NSIG   108
#define OFF_ZERO   109
#define OFF_SIGS   128
#define SIG_LEN    (OTA_SIG_ID_LEN + 64)

static const uint8_t MAGIC[8] = {'T', 'F', 'N', 'O', 'T', 'A', '1', 0};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// the form check: OTA_SIG_OK, OTA_SIG_NONE or OTA_SIG_FORM
static ota_sig_res_t form(const uint8_t *tr, size_t n, uint32_t total)
{
    if (!tr || n < sizeof MAGIC || memcmp(tr, MAGIC, sizeof MAGIC)) {
        return OTA_SIG_NONE;
    }
    if (n < OTA_SIG_TRAILER || total < OTA_SIG_TRAILER) {
        return OTA_SIG_FORM;
    }
    if ((tr[OFF_FORMAT] | tr[OFF_FORMAT + 1] << 8) != 1 || le32(tr + OFF_LEN) != total - OTA_SIG_TRAILER) {
        return OTA_SIG_FORM;
    }
    if (tr[OFF_NSIG] < 1 || tr[OFF_NSIG] > OTA_SIG_MAX) {
        return OTA_SIG_FORM;
    }
    for (int i = OFF_ZERO; i < OFF_SIGS; i++) {
        if (tr[i]) {
            return OTA_SIG_FORM;
        }
    }
    return OTA_SIG_OK;
}

// a text field: the expected text with zero fill, compared with all bytes of the field
static bool field_is(const uint8_t *f, size_t len, const char *want)
{
    size_t k = want ? strlen(want) : 0;
    if (k >= len + (len == OTA_SIG_VER_LEN)) {  // (the version may fill all 32 bytes; the kind keeps one zero)
        return false;
    }
    if (k && memcmp(f, want, k)) {
        return false;
    }
    for (size_t i = k; i < len; i++) {
        if (f[i]) {
            return false;
        }
    }
    return true;
}

bool ota_sig_key_id(const ota_sig_crypto_t *crypto, const uint8_t pub[OTA_SIG_KEY_LEN], uint8_t id[OTA_SIG_ID_LEN])
{
    uint8_t h[32];
    if (!pub || pub[0] != 0x04 || !crypto || !crypto->sha256 || !crypto->sha256(pub, OTA_SIG_KEY_LEN, h)) {
        return false;
    }
    memcpy(id, h, OTA_SIG_ID_LEN);
    return true;
}

bool ota_sig_peek(const uint8_t *tr, size_t n, uint32_t total, uint32_t *sec_ver)
{
    if (form(tr, n, total) != OTA_SIG_OK) {
        return false;
    }
    if (sec_ver) {
        *sec_ver = le32(tr + OFF_SV);
    }
    return true;
}

// the signatures: the first one of a key of the list that verifies (out->key, out->key_id)
static ota_sig_res_t signatures(const uint8_t *tr, const ota_sig_want_t *w, const ota_sig_crypto_t *c,
                                ota_sig_result_t *out)
{
    uint8_t ids[OTA_SIG_KEYS][OTA_SIG_ID_LEN];
    bool used[OTA_SIG_KEYS] = {false};
    int nk = w->keys ? (w->nkeys < OTA_SIG_KEYS ? w->nkeys : OTA_SIG_KEYS) : 0;
    for (int k = 0; k < nk; k++) {
        if (w->keys[k][0] != 0x04) {
            continue;
        }
        if (!ota_sig_key_id(c, w->keys[k], ids[k])) {
            return OTA_SIG_ERR;
        }
        used[k] = true;
    }
    bool named = false;
    for (int i = 0; i < tr[OFF_NSIG]; i++) {
        const uint8_t *s = tr + OFF_SIGS + i * SIG_LEN;
        for (int k = 0; k < nk; k++) {
            if (!used[k] || memcmp(s, ids[k], OTA_SIG_ID_LEN)) {
                continue;
            }
            named = true;
            if (c->verify(w->keys[k], tr, OTA_SIG_SIGNED, s + OTA_SIG_ID_LEN)) {
                out->key = k;
                memcpy(out->key_id, ids[k], OTA_SIG_ID_LEN);
                return OTA_SIG_OK;
            }
        }
    }
    return named ? OTA_SIG_BAD : OTA_SIG_KEY;
}

ota_sig_res_t ota_sig_check(const uint8_t *tr, size_t n, uint32_t total, const uint8_t hash[32],
                            const ota_sig_want_t *want, const ota_sig_crypto_t *crypto, ota_sig_result_t *out)
{
    memset(out, 0, sizeof *out);
    out->key = -1;
    out->res = form(tr, n, total);
    if (out->res != OTA_SIG_OK) {
        return out->res;
    }
    out->has_sv = true;
    out->sec_ver = le32(tr + OFF_SV);
    if (!hash || !want || !crypto || !crypto->sha256 || !crypto->verify) {
        return out->res = OTA_SIG_ERR;
    }
    if (memcmp(tr + OFF_HASH, hash, 32)) {
        return out->res = OTA_SIG_HASH;
    }
    if (!field_is(tr + OFF_KIND, OTA_SIG_KIND_LEN, want->kind) ||
        !field_is(tr + OFF_VER, OTA_SIG_VER_LEN, want->version)) {
        return out->res = OTA_SIG_KIND;
    }
    out->res = signatures(tr, want, crypto, out);
    if (out->res == OTA_SIG_OK && out->sec_ver < want->minsv) {
        out->res = OTA_SIG_OLD;
    }
    return out->res;
}

const char *ota_sig_code(ota_sig_res_t res)
{
    static const char *const CODES[] = {"ok",      "sig_none", "sig_form", "sig_hash", "sig_kind",
                                        "sig_key", "sig_bad",  "sig_old",  "sig_err"};
    return (unsigned)res < sizeof CODES / sizeof CODES[0] ? CODES[res] : "sig_err";
}

const char *ota_sig_why(ota_sig_res_t res)
{
    static const char *const WHY[] = {
            "a trusted key signed the image",
            "no TFNOTA1 trailer at the end of the file",
            "the trailer has a wrong form",
            "the image hash is not the hash in the trailer",
            "the trailer is for another board kind or another version",
            "no signature of a key of the key list",
            "the signature of a key of the list is wrong",
            "the security version is lower than minsv",
            "the board could not do the check (no memory for the hash)",
    };
    return (unsigned)res < sizeof WHY / sizeof WHY[0] ? WHY[res] : WHY[OTA_SIG_ERR];
}

const char *ota_sig_mode_name(ota_sig_mode_t mode)
{
    return mode == OTA_SIG_ENFORCE ? "enforce" : mode == OTA_SIG_REPORT ? "report" : "off";
}

void ota_sig_id_hex(const uint8_t id[OTA_SIG_ID_LEN], char b[17])
{
    for (int i = 0; i < OTA_SIG_ID_LEN; i++) {
        snprintf(b + 2 * i, 3, "%02x", id[i]);
    }
}

int ota_sig_text(const ota_sig_result_t *r, char *b, size_t n)
{
    if (r->res != OTA_SIG_OK) {
        return snprintf(b, n, "%s", ota_sig_code(r->res));
    }
    char h[17];
    ota_sig_id_hex(r->key_id, h);
    return snprintf(b, n, "ok:%s", h);
}

bool ota_sig_accept(ota_sig_mode_t mode, ota_sig_res_t res)
{
    return mode != OTA_SIG_ENFORCE || res == OTA_SIG_OK;
}

bool ota_sig_mark_ok(ota_sig_mode_t mode, bool pending, ota_sig_res_t own)
{
    return mode != OTA_SIG_ENFORCE || !pending || own == OTA_SIG_OK;
}

uint32_t ota_sig_new_minsv(uint32_t minsv, const ota_sig_result_t *own)
{
    return own && own->res == OTA_SIG_OK && own->sec_ver > minsv ? own->sec_ver : minsv;
}

ota_self_t ota_sig_self_test(ota_sig_mode_t mode, bool pending, int64_t now_ms, ota_sig_res_t own, bool web,
                             const char **why)
{
    const char *w = "";
    ota_self_t r = OTA_SELF_MARK;
    if (!pending) {
        r = OTA_SELF_NONE;
    } else if (now_ms < OTA_SELF_MARK_MS) {
        w = "the first 20 s after the start";
        r = OTA_SELF_WAIT;
    } else if (mode != OTA_SIG_ENFORCE) {
        r = OTA_SELF_MARK;  // (REPORT and OFF: the mark after 20 s, with no self-test)
    } else if (!ota_sig_mark_ok(mode, pending, own)) {
        w = "the own trailer does not verify with the own key list";
        r = OTA_SELF_BACK;
    } else if (!web && now_ms < OTA_SELF_WEB_MS) {
        w = "the web server does not run yet (no /api/ota handler)";
        r = OTA_SELF_WAIT;
    } else if (!web) {
        w = "the web server does not run 90 s after the start (no /api/ota handler)";
        r = OTA_SELF_BACK;
    }
    if (why) {
        *why = w;
    }
    return r;
}

ota_back_t ota_sig_back_plan(bool rollback, bool other_tfn, bool web)
{
    if (rollback && other_tfn) {
        return OTA_BACK_ROLLBACK;
    }
    return web ? OTA_BACK_KEEP_MARK : OTA_BACK_KEEP;
}

const char *ota_sig_back_name(ota_back_t b)
{
    return b == OTA_BACK_ROLLBACK ? "rollback" : b == OTA_BACK_KEEP_MARK ? "keep_mark" : "keep";
}

uint32_t ota_sig_copy_len(const uint8_t *after, size_t n, uint32_t image_len)
{
    if (image_len > UINT32_MAX - OTA_SIG_TRAILER) {
        return image_len;
    }
    return form(after, n, image_len + OTA_SIG_TRAILER) == OTA_SIG_OK ? image_len + OTA_SIG_TRAILER : image_len;
}

bool ota_sig_copy_again(bool o0_bad, bool o0_same)
{
    return !(o0_bad && o0_same);
}
