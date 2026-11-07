// The host test of main/web_auth_core.c (the web password: the sessions, the limit of the tries, the parse).
//   sh tests/web_auth/build.sh      -> WEB AUTH CORE OK
#include <stdio.h>
#include <string.h>
#include "web_auth_core.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void rnd(uint8_t r[16], int k)
{
    for (int i = 0; i < 16; i++) r[i] = (uint8_t)(k * 37 + i * 11 + 1);
}

static void test_eq(void)
{
    CHECK(wa_eq("abcd", "abcd", 4));
    CHECK(!wa_eq("abcd", "abce", 4));
    CHECK(wa_eq("", "", 0));
}

static void test_sessions(void)
{
    wa_sess_clear();
    uint8_t r[16];
    char a[WA_TOKEN_LEN + 1], b[WA_TOKEN_LEN + 1];
    rnd(r, 1);
    wa_sess_new(r, 1000, a);
    CHECK(strlen(a) == WA_TOKEN_LEN);
    CHECK(wa_sess_ok(a, 2000));
    char bad[WA_TOKEN_LEN + 1];
    memcpy(bad, a, sizeof bad);
    bad[5] = bad[5] == '0' ? '1' : '0';
    CHECK(!wa_sess_ok(bad, 2000));                  // one changed digit
    CHECK(!wa_sess_ok("abc", 2000));                // a short token
    CHECK(!wa_sess_ok("", 2000));
    CHECK(!wa_sess_ok(NULL, 2000));
    // the idle time: each use starts it again
    int64_t t = 2000;
    for (int i = 0; i < 5; i++) { t += WA_IDLE_MS - 1000; CHECK(wa_sess_ok(a, t)); }
    CHECK(!wa_sess_ok(a, t + WA_IDLE_MS));          // a day with no request
    CHECK(!wa_sess_ok(a, t + 1000));                // (and it stays ended)
    // the life: used each hour, it still ends 30 days after the login
    rnd(r, 2);
    wa_sess_new(r, 0, b);
    for (t = 3600000; t < WA_LIFE_MS; t += 3600000) if (!wa_sess_ok(b, t)) break;
    CHECK(t >= WA_LIFE_MS - 3600000);
    CHECK(!wa_sess_ok(b, WA_LIFE_MS + 1));
    // drop, clear
    rnd(r, 3);
    wa_sess_new(r, 0, a);
    CHECK(wa_sess_ok(a, 10));
    wa_sess_drop(a);
    CHECK(!wa_sess_ok(a, 20));
    rnd(r, 4);
    wa_sess_new(r, 0, a);
    wa_sess_clear();
    CHECK(!wa_sess_ok(a, 10));
    // a 9th login drops the session that was used the longest time ago
    char tok[WA_SESS_MAX + 1][WA_TOKEN_LEN + 1];
    for (int i = 0; i < WA_SESS_MAX; i++) { rnd(r, 10 + i); wa_sess_new(r, 100 + i, tok[i]); }
    CHECK(wa_sess_count(200) == WA_SESS_MAX);
    for (int i = 1; i < WA_SESS_MAX; i++) CHECK(wa_sess_ok(tok[i], 300 + i));     // all but the first are used again
    rnd(r, 99);
    wa_sess_new(r, 400, tok[WA_SESS_MAX]);
    CHECK(!wa_sess_ok(tok[0], 500));
    for (int i = 1; i <= WA_SESS_MAX; i++) CHECK(wa_sess_ok(tok[i], 500));
    CHECK(wa_sess_count(600) == WA_SESS_MAX);
    wa_sess_clear();
}

static void test_tries(void)
{
    wa_tries_clear();
    const uint32_t ip = 0x0a00005e, other = 0x0a00005f;
    int64_t t = 1000;
    for (int i = 0; i < WA_FREE_TRIES - 1; i++) { wa_fail(ip, t); CHECK(wa_wait_ms(ip, t) == 0); }
    wa_fail(ip, t);                                 // the 5th wrong password: a wait
    CHECK(wa_wait_ms(ip, t) == WA_WAIT0_MS);
    CHECK(wa_wait_ms(ip, t + WA_WAIT0_MS - 1) == 1);
    CHECK(wa_wait_ms(ip, t + WA_WAIT0_MS) == 0);
    CHECK(wa_wait_ms(other, t) == 0);               // another address can try
    t += WA_WAIT0_MS;
    wa_fail(ip, t);                                 // the 6th: the wait doubles
    CHECK(wa_wait_ms(ip, t) == 2 * WA_WAIT0_MS);
    for (int i = 0; i < 20; i++) { t += WA_WAIT_MAX_MS; wa_fail(ip, t); }
    CHECK(wa_wait_ms(ip, t) == WA_WAIT_MAX_MS);     // at most 15 min
    wa_pass(ip);                                    // the right password: the count goes
    CHECK(wa_wait_ms(ip, t) == 0);
    for (int i = 0; i < WA_FREE_TRIES - 1; i++) wa_fail(ip, t);
    CHECK(wa_wait_ms(ip, t) == 0);
    // more addresses than places: the oldest goes (never a crash), the newest keeps its count
    wa_tries_clear();
    for (uint32_t k = 0; k < 3 * WA_IP_MAX; k++) for (int i = 0; i < WA_FREE_TRIES; i++) wa_fail(100 + k, 5000 + k);
    CHECK(wa_wait_ms(100 + 3 * WA_IP_MAX - 1, 5000 + 3 * WA_IP_MAX - 1) > 0);
    wa_tries_clear();
    CHECK(wa_wait_ms(100 + 3 * WA_IP_MAX - 1, 5000) == 0);
}

static void test_basic(void)
{
    char pw[64];
    CHECK(wa_basic_password("Basic OnNlY3JldA==", pw, sizeof pw) && !strcmp(pw, "secret"));        // ":secret"
    CHECK(wa_basic_password("Basic dXNlcjpwYXNz", pw, sizeof pw) && !strcmp(pw, "pass"));          // "user:pass"
    CHECK(wa_basic_password("basic dXNlcjpwYXNz", pw, sizeof pw) && !strcmp(pw, "pass"));
    CHECK(wa_basic_password("  Basic   OmE6Yg==", pw, sizeof pw) && !strcmp(pw, "a:b"));           // ":a:b"
    CHECK(wa_basic_password("Basic Og==", pw, sizeof pw) && !strcmp(pw, ""));                      // ":" (empty)
    CHECK(!wa_basic_password("Basic dXNlcg==", pw, sizeof pw));                                    // "user": no ':'
    CHECK(!wa_basic_password("Bearer OnNlY3JldA==", pw, sizeof pw));
    CHECK(!wa_basic_password("Basic ***", pw, sizeof pw));
    CHECK(!wa_basic_password("Basic AHg6eQ==", pw, sizeof pw));                                    // "\0x:y"
    CHECK(!wa_basic_password("Basic OnNlY3JldA==", pw, 4));                                        // no room
    CHECK(!wa_basic_password(NULL, pw, sizeof pw));
    char lng[400] = "Basic ";
    for (int i = 6; i < 390; i++) lng[i] = 'Q';
    lng[390] = 0;
    CHECK(!wa_basic_password(lng, pw, sizeof pw));                                                 // too long
}

static void test_next(void)
{
    char o[64];
    wa_next_path("/walkie", o, sizeof o);       CHECK(!strcmp(o, "/walkie"));
    wa_next_path("/api/apps?x=1", o, sizeof o); CHECK(!strcmp(o, "/api/apps?x=1"));
    wa_next_path("//evil.com/x", o, sizeof o);  CHECK(!strcmp(o, "/"));
    wa_next_path("/\\evil.com", o, sizeof o);   CHECK(!strcmp(o, "/"));
    wa_next_path("http://evil.com", o, sizeof o); CHECK(!strcmp(o, "/"));
    wa_next_path("/a\r\nSet-Cookie: x", o, sizeof o); CHECK(!strcmp(o, "/"));
    wa_next_path("/login?next=/login", o, sizeof o); CHECK(!strcmp(o, "/"));
    wa_next_path("", o, sizeof o);              CHECK(!strcmp(o, "/"));
    wa_next_path(NULL, o, sizeof o);            CHECK(!strcmp(o, "/"));
    char lng[100];
    memset(lng, 'a', sizeof lng);
    lng[0] = '/';
    lng[99] = 0;
    wa_next_path(lng, o, sizeof o);             CHECK(!strcmp(o, "/"));            // longer than the room
}

static void test_form(void)
{
    char v[32];
    const char *b = "p=a%20b+c%21&next=%2Fwalkie%3Fx%3D1&pp=no";
    CHECK(wa_form_field(b, "p", v, sizeof v) && !strcmp(v, "a b c!"));
    CHECK(wa_form_field(b, "next", v, sizeof v) && !strcmp(v, "/walkie?x=1"));
    CHECK(wa_form_field(b, "pp", v, sizeof v) && !strcmp(v, "no"));
    CHECK(!wa_form_field(b, "q", v, sizeof v) && v[0] == 0);
    CHECK(wa_form_field("p=", "p", v, sizeof v) && v[0] == 0);
    CHECK(wa_form_field("p=%4", "p", v, sizeof v) && !strcmp(v, "%4"));       // a cut %xx stays as it is
    CHECK(wa_form_field("p=%zz", "p", v, sizeof v) && !strcmp(v, "%zz"));
    CHECK(!wa_form_field("p=0123456789", "p", v, 6));                          // no room
    CHECK(wa_form_field("x=1&p=%00a", "p", v, sizeof v) && v[0] == 0);         // (an encoded 0: the text ends there)
}

int main(void)
{
    test_eq();
    test_sessions();
    test_tries();
    test_basic();
    test_next();
    test_form();
    if (s_fail) {
        printf("WEB AUTH CORE: %d FAILED\n", s_fail);
        return 1;
    }
    printf("WEB AUTH CORE OK\n");
    return 0;
}
