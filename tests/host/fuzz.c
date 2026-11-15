// Host fuzz of all apps (clang + ASan/UBSan): random touches, two-finger points, end buttons,
// IMU values (tilt, linear, shake, hits, snaps, dial, steps), the microphone, the audio tick,
// frame steps of 12, 16 and 33 ms with long frames, and leave/enter. Build with build.sh for the
// 3.49 (inc349), the CoreS3 (incs3) and the Watcher (incwt). Usage: fuzz.exe [frames per app] [seed]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "gfx.h"
#include "input.h"
#include "imu.h"
#include "board.h"

extern float sim_tx, sim_ty, sim_shake, sim_ax, sim_ay, sim_mic, sim_dial, sim_turn_rate;
extern uint32_t sim_steps;
extern void (*sim_tick)(void);
extern long n_voices, n_sfx;
extern int16_t sim_pt_x[INPUT_MAX_POINTS], sim_pt_y[INPUT_MAX_POINTS];
extern int sim_npt;
extern bool sim_multi;
extern uint8_t sim_caps;
extern const app_t *sim_opened;
extern int sim_wheel_steps, sim_wheel_presses;
void sim_add_hit(float x, float y, float z, float g);
void sim_add_snap(float x, float y, float v);
void sim_end(int e, bool down, int64_t t);
void sim_capture_block(void);
void sim_mic_block(void);

static int64_t s_now_ms;
int64_t esp_timer_get_time(void) { return s_now_ms * 1000; }
uint32_t esp_random(void) { return (uint32_t)rand() * 2654435761u ^ (uint32_t)rand(); }

// all apps of the list that tests/host/build.sh makes (main/apps_gen.py --host: ALL apps of main/apps.json, also the
// apps that are off in the firmware, so that they keep compiling)
static const app_t *const APPS[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) &sym,
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
#define N_APPS ((int)(sizeof(APPS) / sizeof(APPS[0])))

static float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

static void event(const app_t *a, input_kind_t k, int x, int y)
{
    input_event_t ev = {.kind = k, .x = (int16_t)x, .y = (int16_t)y, .t_ms = s_now_ms};
    a->input(&ev);
}

// one app: enter, random frames, leave; returns the frames with unpainted pixels
static int run_app(const app_t *a, int frames)
{
#if BOARD_ROUND
    gfx_set_native(a->round);       // (main.c: a round app gets the native 412 x 412 frame)
#endif
    gfx_set_orient(a->landscape ? ORIENT_LAND_A : ORIENT_PORTRAIT);
    sim_opened = NULL;
    a->enter(s_now_ms);
    bool down = false;
    int fx = 0, fy = 0, unpainted = 0;
    for (int f = 0; f < frames; f++) {
        int r = rand() % 100;
        int dt = r < 40 ? 16 : r < 70 ? 12 : r < 98 ? 33 : 180;
        s_now_ms += dt;
        // touches in logical coordinates (sometimes a little outside the screen)
        int ev = rand() % 10;
        if (!down && ev < 3) {
            fx = (int)frand(-4, gfx_w + 4);
            fy = (int)frand(-4, gfx_h + 4);
            event(a, IN_DOWN, fx, fy);
            down = true;
        } else if (down && ev < 6) {
            fx += rand() % 41 - 20;
            fy += rand() % 41 - 20;
            event(a, IN_MOVE, fx, fy);
        } else if (down && ev < 8) {
            event(a, IN_UP, fx, fy);
            if (rand() % 2) event(a, IN_TAP, fx, fy);
            down = false;
        } else if (down && ev == 8 && rand() % 4 == 0) {
            event(a, IN_LONG, fx, fy);
        }
        if (sim_opened) {
            break;          // the app asked for another app (for example the menu)
        }
        // two fingers (physical), the end buttons, the sensors and the microphone
        sim_npt = rand() % 4 == 0 ? rand() % (INPUT_MAX_POINTS + 1) : rand() % 3;
        for (int i = 0; i < INPUT_MAX_POINTS; i++) {
            sim_pt_x[i] = (int16_t)(rand() % LCD_W);
            sim_pt_y[i] = (int16_t)(rand() % LCD_H);
        }
        if (rand() % 6 == 0) sim_end(rand() % 2, rand() % 2, s_now_ms);
        sim_multi = rand() % 5 != 0;
        sim_tx += frand(-0.08f, 0.08f);
        sim_ty += frand(-0.08f, 0.08f);
        if (sim_tx > 1) sim_tx = 1;
        if (sim_tx < -1) sim_tx = -1;
        if (sim_ty > 1) sim_ty = 1;
        if (sim_ty < -1) sim_ty = -1;
        sim_ax = frand(-0.5f, 0.5f);
        sim_ay = frand(-0.5f, 0.5f);
        sim_shake = rand() % 20 == 0 ? frand(0, 4) : frand(0, 0.3f);
        sim_mic = rand() % 10 == 0 ? frand(0, 1) : frand(0, 0.05f);
        sim_turn_rate = frand(-3, 3);
        sim_dial += sim_turn_rate * dt / 1000.0f;
        if (rand() % 25 == 0) sim_steps++;
        if (rand() % 6 == 0) sim_wheel_steps += rand() % 7 - 3;         // the wheel (the Watcher)
        if (rand() % 40 == 0) sim_wheel_presses++;
        if (rand() % 30 == 0) sim_add_hit(frand(-1, 1), frand(-1, 1), frand(-1, 1), frand(1.3f, 4));
        if (rand() % 40 == 0) sim_add_snap(frand(-1, 1), frand(-1, 1), frand(0.3f, 2));
        // the audio task: one tick and one microphone block for each 5 ms
        for (int t = 0; t < dt / 5; t++) {
            if (sim_tick) sim_tick();
            sim_capture_block();
            sim_mic_block();
        }
        a->update(s_now_ms);
        // the full-paint check: two fill values; a pixel that keeps the fill is not painted
        int n = gfx_w * gfx_h, miss = 0;
        if (f % 97 == 0) {
            for (int pass = 0; pass < 2; pass++) {
                uint16_t fill = pass ? 0xA5A5 : 0x5A5A;
                for (int i = 0; i < n; i++) g_fb[i] = fill;
                gfx_clip_reset();
                a->render(s_now_ms);
                int m = 0, first = -1;
                for (int i = 0; i < n; i++) {
                    // (a round layout: only the circle shows on the panel; the app need not paint the pixels out of it)
                    if (g_fb[i] == fill && (!gfx_round() || gfx_in_circle(i % gfx_w, i / gfx_w, 1))) { m++; if (first < 0) first = i; }
                }
                if (pass == 0) miss = m;
                else if (m && miss) {
                    if (!unpainted) {
                        printf("    (%s: frame %d, %d pixels not painted, the first at %d,%d, size %dx%d)\n", a->name, f, m,
                               first % gfx_w, first / gfx_w, gfx_w, gfx_h);
                        // (a picture of the frame: the pixels that are not painted are magenta; fuzz_unpainted.ppm)
                        FILE *pf = fopen("fuzz_unpainted.ppm", "wb");
                        if (pf) {
                            fprintf(pf, "P6\n%d %d\n255\n", gfx_w, gfx_h);
                            for (int i = 0; i < n; i++) {
                                uint16_t c = (uint16_t)((g_fb[i] >> 8) | (g_fb[i] << 8));
                                if (g_fb[i] == fill) { fputc(255, pf); fputc(0, pf); fputc(255, pf); continue; }
                                fputc((c >> 11) << 3, pf); fputc(((c >> 5) & 63) << 2, pf); fputc((c & 31) << 3, pf);
                            }
                            fclose(pf);
                        }
                    }
                    unpainted++;
                }
            }
        } else {
            gfx_clip_reset();
            a->render(s_now_ms);
        }
        // the menu follows the board into all four orientations
        if (!BOARD_FIXED_ORIENT && a == &app_menu && rand() % 200 == 0) {
            gfx_set_orient((orient_t)(rand() % 4));
        }
        // sometimes leave and come back
        if (rand() % 1500 == 0) {
            if (a->leave) a->leave();
            a->enter(s_now_ms);
        }
    }
    if (a->leave) a->leave();
    sim_tick = NULL;
    return unpainted;
}

// the apps that only some board kinds have ("only" in main/apps.json, apps_gen/only.txt: the store apps of the 3.49):
// this run skips them on the other kinds (they draw into a frame of the 3.49 size)
#if BOARD_BAR_SCREEN
#define HOST_KIND "lcd349"
#elif BOARD_WHEEL
#define HOST_KIND "watcher"
#else
#define HOST_KIND "cores3"
#endif
static bool not_here(const char *name)
{
    static char buf[4096];
    static int n = -1;
    if (n < 0) {
        FILE *f = fopen("apps_gen/only.txt", "rb");
        n = f ? (int)fread(buf, 1, sizeof buf - 1, f) : 0;
        if (f) fclose(f);
        buf[n] = 0;
    }
    size_t nl = strlen(name);
    for (const char *l = buf; *l;) {
        const char *e = strchr(l, '\n'), *t = strchr(l, '\t');
        if (!e) e = l + strlen(l);
        if (t && t < e && (size_t)(t - l) == nl && !strncmp(l, name, nl)) {
            char kinds[128];
            size_t kl = (size_t)(e - t - 1);
            if (kl >= sizeof kinds) kl = sizeof kinds - 1;
            memcpy(kinds, t + 1, kl);
            kinds[kl] = 0;
            return !strstr(kinds, HOST_KIND);
        }
        l = *e ? e + 1 : e;
    }
    return false;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);       // the stub messages must come out before an abort()
    int frames = argc > 1 ? atoi(argv[1]) : 6000;
    srand(argc > 2 ? (unsigned)atoi(argv[2]) : 1);
    gfx_init();
    for (int i = 0; i < N_APPS; i++) {
        if (APPS[i]->init) APPS[i]->init();
    }
    int bad = 0;
    for (int caps = 0; caps < 2; caps++) {
#if BOARD_BAR_SCREEN
        if (caps == 1) break;               // the 3.49 has the IMU and the bar screen: one pass
        sim_caps = APP_NEEDS_IMU | APP_NEEDS_BAR;      // (without the bar, the apps that need it are skipped)
#else
        sim_caps = caps == 0 ? APP_NEEDS_IMU : 0;     // the full CoreS3, then the SE
#endif
        for (int i = 0; i < N_APPS; i++) {
            if (APPS[i]->needs & ~sim_caps) continue;
            if (not_here(APPS[i]->name)) continue;
            // FUZZ_SKIP="NAME,NAME": apps that this run skips (for example the store apps of the 3.49 only on the
            // CoreS3 and Watcher layouts)
            const char *skip = getenv("FUZZ_SKIP");
            if (skip) {
                const char *p = strstr(skip, APPS[i]->name);
                size_t ln = strlen(APPS[i]->name);
                if (p && (p == skip || p[-1] == ',') && (p[ln] == 0 || p[ln] == ',')) continue;
            }
            long v0 = n_voices, s0 = n_sfx;
            int u = run_app(APPS[i], frames);
            printf("  %-8s %s %d frames ok, %ld voices, %ld sfx%s\n", APPS[i]->name,
                   sim_caps ? "IMU   " : "no IMU", frames, n_voices - v0, n_sfx - s0,
                   u ? "  UNPAINTED FRAMES" : "");
            fflush(stdout);
            bad += u != 0;
        }
    }
    printf(bad ? "%d apps with unpainted frames\n" : "all ok\n", bad);
    return bad != 0;
}
