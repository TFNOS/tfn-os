// TFN OS SDK: the icon frames and the app_t flags of one app, on the PC (sdk/tools/mk_app.py runs it). All apps of
// main/apps.json are linked. For each frame (a time step) the tool draws the icon of
// the app (its draw_icon, as the menu does) two times, on black and on white: a pixel that is the same on both is a
// pixel of the icon, another one is a hole (mk_app.py makes icon.bin from them).
//   iconshot_<b>.exe "<app name>" <size> <frames> <ms> <out.raw>
// stdout: the app_t flags as manifest lines (landscape=1 ...); out.raw: frames x 2 x size x size RGB565 (g_fb values)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "gfx.h"
#include "board.h"

static int64_t s_now_ms = 5000;
int64_t esp_timer_get_time(void) { return s_now_ms * 1000; }
static uint32_t s_rnd = 12345;
uint32_t esp_random(void) { s_rnd = s_rnd * 1103515245u + 12345u; return s_rnd ^ (s_rnd >> 13); }

#ifdef ICON_EXT
// an app out of main/ (mk_app.py with a .c file): only that app, linked with the library of build.sh
extern const app_t ICON_EXT;
static const app_t *const APPS[] = {&ICON_EXT};
#else
static const app_t *const APPS[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) &sym,
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
#endif
#define N_APPS ((int)(sizeof(APPS) / sizeof(APPS[0])))

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 6) { printf("usage: iconshot \"APP\" size frames ms out.raw\n"); return 2; }
    int size = atoi(argv[2]), frames = atoi(argv[3]), ms = atoi(argv[4]);
    const app_t *app = NULL;
    gfx_init();
    for (int i = 0; i < N_APPS; i++) if (!strcmp(APPS[i]->name, argv[1])) app = APPS[i];
#ifdef ICON_EXT
    app = APPS[0];                          // (the one app of the file, whatever its name)
#endif
    if (!app) { printf("no app %s\n", argv[1]); return 2; }
    if (size < 8 || size > 160 || frames < 1 || frames > 64) { printf("bad size or frames\n"); return 2; }
    for (int i = 0; i < N_APPS; i++) if (APPS[i]->init) APPS[i]->init();
    // the menu frame: landscape (the icons use landscape logical coordinates)
    gfx_set_native(false);
    gfx_set_orient(ORIENT_LAND_A);
    if (gfx_w < size + 16 || gfx_h < size + 16) { printf("the frame %dx%d is too small for %d\n", gfx_w, gfx_h, size); return 2; }
    FILE *f = fopen(argv[5], "wb");
    if (!f) { printf("cannot write %s\n", argv[5]); return 2; }
    const int x = 8, y = 8;
    for (int fr = 0; fr < frames; fr++) {
        for (int bg = 0; bg < 2; bg++) {
            s_now_ms = 5000 + (int64_t)fr * ms;
            uint16_t c = bg ? 0xFFFF : 0x0000;
            for (int i = 0; i < gfx_w * gfx_h; i++) g_fb[i] = c;
            gfx_clip_box(x, y, x + size, y + size);
            app->draw_icon(x, y, size);
            gfx_clip_box_off();
            gfx_clip_reset();
            for (int r = 0; r < size; r++) fwrite(&g_fb[(y + r) * gfx_w + x], 2, (size_t)size, f);
        }
    }
    fclose(f);
    printf("landscape=%d\nnative=%d\nround=%d\nround_fit=%d\nwheel=%d\nkey=%d\nrun=%d\nneeds=%d\n", app->landscape,
           app->native, app->round, app->round_fit, app->wheel, app->key, app->run, app->needs);
    printf("has_init=%d\nhas_awake=%d\nhas_background=%d\nhas_keep_on=%d\n", app->init != NULL, app->awake != NULL,
           app->background != NULL, app->keep_on != NULL);
    return 0;
}
