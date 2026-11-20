// TFNCOMP (libs/tfncomp, the protocol of a TFN COMPANION): the keys (RFC 7748, SipHash vectors), the frames, the tags,
// the parts of a long frame, the pins of each chip.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfncomp/tfncomp.h"

static int s_bad;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            s_bad++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static void hex(const char *s, uint8_t *o, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        o[i] = (uint8_t)v;
    }
}

static void t_x25519(void)
{
    uint8_t k[32], u[32], want[32], out[32];
    hex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k, 32);
    hex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
    hex("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", want, 32);
    tc_x25519(out, k, u);
    CHECK(!memcmp(out, want, 32), "RFC 7748 5.2");
    uint8_t a[32], b[32], pa[32], pb[32], sa[32], sb[32];
    hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    tc_x25519_base(pa, a);
    tc_x25519_base(pb, b);
    hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", want, 32);
    CHECK(!memcmp(pa, want, 32), "alice pub");
    hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", want, 32);
    CHECK(!memcmp(pb, want, 32), "bob pub");
    tc_x25519(sa, a, pb);
    tc_x25519(sb, b, pa);
    hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want, 32);
    CHECK(!memcmp(sa, want, 32) && !memcmp(sb, want, 32), "shared");
}

static void t_siphash(void)
{
    uint8_t key[16], msg[15];
    for (int i = 0; i < 16; i++) {
        key[i] = (uint8_t)i;
    }
    for (int i = 0; i < 15; i++) {
        msg[i] = (uint8_t)i;
    }
    CHECK(tc_siphash(key, NULL, 0, NULL, 0) == 0x726fdb47dd0e0e31ULL, "siphash empty");
    CHECK(tc_siphash(key, msg, 15, NULL, 0) == 0xa129ca6149be45e5ULL, "siphash 15");
    CHECK(tc_siphash(key, msg, 6, msg + 6, 9) == 0xa129ca6149be45e5ULL, "siphash split");
}

static void t_frames(void)
{
    const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t mac[6] = {0xA0, 0xA3, 0xB3, 0xFE, 0xB4, 0xB8};
    tc_buf_t f;
    tc_begin(&f, TC_PING, 77, 0, 6 | TC_CH_WIFI);
    tc_put32(&f, 0x12345678);
    CHECK(tc_seal(&f, key, mac, 1000), "seal");
    tc_msg_t m;
    CHECK(tc_parse(f.b, f.n, &m) && m.type == TC_PING && m.seq == 77 && m.counter == 1000 && m.blen == 4, "parse");
    CHECK(tc_get32(&m) == 0x12345678 && !m.bad, "body");
    CHECK(tc_check(f.b, f.n, key, mac), "tag ok");
    uint8_t other[6];
    memcpy(other, mac, 6);
    other[5] ^= 1;
    CHECK(!tc_check(f.b, f.n, key, other), "other mac");
    for (int i = 0; i < f.n; i++) {
        f.b[i] ^= 0x10;
        CHECK(!tc_check(f.b, f.n, key, mac), "a changed byte %d", i);
        f.b[i] ^= 0x10;
    }
    // a full body
    tc_begin(&f, TC_FRAME, 1, 0, 1);
    uint8_t big[TC_BODY_MAX + 1];
    memset(big, 7, sizeof big);
    tc_putn(&f, big, TC_BODY_MAX);
    CHECK(!f.bad && tc_seal(&f, key, mac, 1) && f.n == TC_FRAME_MAX, "full frame %d", f.n);
    tc_begin(&f, TC_FRAME, 1, 0, 1);
    tc_putn(&f, big, TC_BODY_MAX + 1);
    CHECK(f.bad, "too big");
    CHECK(!tc_parse((const uint8_t *)"CP", 2, &m), "short");
}

static void t_parts(void)
{
    // a long air-conditioner frame: 600 level times, some longer than 32767 us
    static int32_t d[700], back[700];
    int n = 0;
    for (int i = 0; i < 600; i++) {
        d[n++] = (i & 1) ? -(400 + (i % 7) * 900) : 560;
    }
    d[101] = -40000;
    static uint16_t v[TC_VALS_MAX];
    const int nv = tc_vals_from_levels(d, n, v, TC_VALS_MAX);
    CHECK(nv == 601, "values %d", nv);
    tc_part_t head = {.id = 9, .pin = 4, .carrier = 38, .rep_ms = 108, .rep_at = (uint16_t)nv};
    static tc_part_t parts[8];
    const int np = tc_parts_make(&head, v, nv, parts, 8);
    CHECK(np == 7, "parts %d", np);
    // over the "air", in a bad order, with a lost part sent again
    const uint8_t key[16] = {9};
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    static tc_asm_t a;
    memset(&a, 0, sizeof a);
    const int order[] = {3, 0, 6, 1, 2, 4};  // part 5 is lost
    int r = 0;
    for (int k = 0; k < 6; k++) {
        tc_buf_t f;
        tc_begin(&f, TC_SEND, (uint16_t)k, 0, 1);
        tc_part_put(&f, &parts[order[k]]);
        CHECK(!f.bad && tc_seal(&f, key, mac, (uint32_t)k + 1), "part frame %d (%d bytes)", k, f.n);
        tc_msg_t m;
        tc_part_t p;
        CHECK(tc_parse(f.b, f.n, &m) && tc_part_get(&m, &p), "part get");
        r = tc_asm_add(&a, &p);
    }
    CHECK(r == 0 && a.got == 0x5F, "missing part 5: got %02x", a.got);
    r = tc_asm_add(&a, &parts[5]);
    CHECK(r == 1, "complete");
    const int nb = tc_levels_from_vals(a.v, a.total, back, 700);
    CHECK(nb == n && !memcmp(back, d, sizeof(int32_t) * (size_t)n), "the same level times (%d)", nb);
    CHECK(a.head.rep_ms == 108 && a.head.carrier == 38 && a.head.pin == 4, "head fields");
}

static void t_pins(void)
{
    tc_wire_t w[4] = {
            {2, TC_R_IR_RX, 0, 0}, {4, TC_R_IR_TX, 0, 38}, {35, TC_R_RF433_TX, 0, 0}, {4, TC_R_RF433_RX, 0, 0}};
    uint8_t e[4], wn[4];
    CHECK(!tc_wire_check(TC_CHIP_ESP32, w, 4, e, wn), "check");
    CHECK(e[0] == TC_E_OK && (wn[0] & TC_W_STRAP) && (wn[0] & TC_W_LED), "pin 2");
    CHECK(e[1] == TC_E_OK && wn[1] == 0, "pin 4");
    CHECK(e[2] == TC_E_IN_ONLY, "pin 35 tx");
    CHECK(e[3] == TC_E_DUP, "dup");
    tc_wire_t f1 = {7, TC_R_IR_RX, 0, 0};
    CHECK(!tc_wire_check(TC_CHIP_ESP32, &f1, 1, e, wn) && e[0] == TC_E_FLASH, "flash pin");
    tc_wire_t f2 = {19, TC_R_IR_RX, 0, 0};
    CHECK(!tc_wire_check(TC_CHIP_S3, &f2, 1, e, wn) && e[0] == TC_E_USB, "usb pin");
    tc_wire_t f3 = {21, TC_R_IR_RX, 0, 0};
    CHECK(!tc_wire_check(TC_CHIP_C3, &f3, 1, e, wn) && e[0] == TC_E_CONSOLE, "console pin");
    // the info both ways
    tc_info_t in;
    memset(&in, 0, sizeof in);
    in.chip = TC_CHIP_ESP32;
    in.rev = 3;
    in.fw = 1;
    in.ngpio = (uint8_t)tc_chip_gpios(TC_CHIP_ESP32);
    for (int g = 0; g < in.ngpio; g++) {
        in.pins[g] = tc_pin_flags(TC_CHIP_ESP32, g);
    }
    in.led = 2;
    in.nwire = 2;
    memcpy(in.wire, w, 2 * sizeof w[0]);
    tc_buf_t f;
    tc_begin(&f, TC_INFO, 5, 0, 1);
    tc_info_put(&f, &in);
    CHECK(!f.bad, "info fits (%d)", f.n);
    tc_msg_t m;
    tc_info_t out;
    CHECK(tc_parse(f.b, f.n, &m) && tc_info_get(&m, &out) && !memcmp(&in, &out, sizeof in), "info back");
}

int main(void)
{
    t_x25519();
    t_siphash();
    t_frames();
    t_parts();
    t_pins();
    printf(s_bad ? "TFNCOMP FAIL (%d)\n" : "TFNCOMP OK\n", s_bad);
    return s_bad ? 1 : 0;
}
