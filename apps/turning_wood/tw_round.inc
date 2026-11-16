// THE TURNING WOOD: the round layout (the round panels: app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the
// Watcher 412 x 412). The pictures are 640 x 240. A place, the title and the end: a square view of half the panel
// (233 x 233 on the 1.75C) is drawn with the code of the 320 x 240 frame, then each pixel becomes 2 x 2: the place
// fills the whole circle. A close-up, the journal and the map: a frame of the size of their layout scaled to the width
// of the circle; the bands above and below it get its edge rows, darker to the edge. The HUD is drawn at the size of
// the panel on the arcs: the bag, the journal and the map at the top cap, the bag strip and the message at the bottom
// cap, the X in the top band. A drag still looks round the picture; a swipe walks.
#if BOARD_ROUND
static int s_rv_lw, s_rv_lh;  // the frame of the last draw or input (0: none yet: no move of the view)

// the frame of the screen now: the square view (a place, the title, the end) or the frame of the layout
static void rnd_frame(int *w, int *h)
{
    const int d = rv_panel(&s_rv);
    if (s_mode == M_PLACE || s_mode == M_TITLE || s_mode == M_END || s_mode == M_NODATA) {
        *w = *h = d / 2;
    } else if (s_mode == M_CU) {
        *w = 344;  // (the 320 x 240 layout and a little of the picture round it: the parts stay inside the circle)
        *h = 240;
    } else {
        *w = 360;  // (the journal and the map: their layout from gfx_w; the names at the edges stay inside the circle)
        *h = 240;
    }
}
// the frame of the mode now; when the frame changed, the view keeps its middle
static void rnd_use(void)
{
    int w, h;
    rnd_frame(&w, &h);
    rv_frame(&s_rv, w, h);
    if (s_rv_lw && (w != s_rv_lw || h != s_rv_lh)) {
        s_vx += (s_rv_lw - w) / 2;
        s_vy += (s_rv_lh - h) / 2;
    }
    s_rv_lw = w;
    s_rv_lh = h;
    if (s_vx > PW - w) {
        s_vx = PW - w;
    }
    if (s_vx < 0) {
        s_vx = 0;
    }
    if (s_vy > PH - h) {
        s_vy = PH - h;
    }
    if (s_vy < 0) {
        s_vy = 0;
    }
}
// the code of the 320 x 240 frame runs in the frame of the mode now (gfx_w, gfx_h = the frame)
static void rnd_logic_on(void)
{
    rnd_use();
    rv_enter(&s_rv, false);
    s_rv_logic = true;
}
static void rnd_logic_off(void)
{
    s_rv_logic = false;
    rv_leave(&s_rv);
}
// the X key (a close-up, the journal, the map): the top band; the points of the panel
static void rnd_x_xy(int *x, int *y, int *r)
{
    const int d = rv_panel(&s_rv);
    *x = d / 2;
    *y = d * 9 / 100;
    *r = 26;
}
static bool rnd_x_hit(int x, int y)
{
    int bx, by, br;
    rnd_x_xy(&bx, &by, &br);
    return (x - bx) * (x - bx) + (y - by) * (y - by) < (br + 8) * (br + 8);
}
static int bag_top(void);
static bool bag_tap(int px, int py);
static void rnd_input_hud(const input_event_t *ev)
{
    const int x = ev->x, y = ev->y;
    if (ev->kind == IN_TAP) {
        if (s_mode == M_TITLE) {
            title_tap(x, y);
            return;
        }
        if (s_mode == M_CU && rnd_x_hit(x, y)) {
            close_cu();
            return;
        }
        if ((s_mode == M_JOURNAL || s_mode == M_MAP) && rnd_x_hit(x, y)) {
            if (s_mode == M_JOURNAL) {
                spr_free(&s_jspr);
                s_jspr_for = -1;
            }
            s_mode = M_PLACE;
            s_map_sel = -1;
            return;
        }
        if (s_mode == M_PLACE) {
            if (btn_hit(x, y) >= 0) {
                input_main(ev);  // (the buttons: the same code; btn_hit gives the same answer)
                return;
            }
            if (s_bag) {
                if (y >= bag_top()) {
                    bag_tap(x, y);
                    return;
                }
                s_bag = false;
            }
        }
    }
    if (s_mode == M_PLACE && s_bag && y >= bag_top() && ev->kind != IN_TAP) {
        input_main(ev);  // (a drag in the bag scrolls it: the points of the panel)
        return;
    }
    input_event_t e = *ev;
    int fx_ = x, fy_ = y;
    rv_point(&s_rv, &fx_, &fy_);
    e.x = (int16_t)fx_;
    e.y = (int16_t)fy_;
    s_rv_logic = true;
    input_main(&e);
    s_rv_logic = false;
}
// the input of the round frame: the HUD at the size of the panel, then the rest with a point of the frame (all of it
// in the frame of the mode now: the actions of the HUD move the view of the frame)
static void rnd_input(const input_event_t *ev)
{
    rnd_use();
    rv_enter(&s_rv, false);
    rnd_input_hud(ev);
    rv_leave(&s_rv);
}
static void rnd_draw_title(void)
{
    const int d = gfx_w, ty = d * 28 / 100;
    ui_text_c(&KF_TITLE, d / 2 + 2, ty - 16 + 2, TR("THE TURNING", "היער"), rgb(20, 14, 10));
    ui_text_c(&KF_TITLE, d / 2, ty - 16, TR("THE TURNING", "היער"), rgb(250, 238, 210));
    ui_text_c(&KF_TITLE, d / 2 + 2, ty + 20 + 2, TR("WOOD", "הסובב"), rgb(20, 14, 10));
    ui_text_c(&KF_TITLE, d / 2, ty + 20, TR("WOOD", "הסובב"), rgb(250, 238, 210));
    for (int i = 0; i < 2; i++) {
        if (i == 0 && !s_has) {
            continue;
        }
        int x, y;
        title_btn(i, &x, &y);
        const bool armed = i == 1 && s_title_dn == 1;
        ui_circle(x, y, 36, armed ? rgb(200, 120, 60) : rgb(30, 22, 14), 220);
        ui_ring(x, y, 36, 2, rgb(201, 162, 79), 255);
        ui_text_c(&KF_NAME, x, y + KF_NAME.asc / 2 - 1,
                  i == 0 || !s_has ? TR("PLAY", "שחק")
                  : armed          ? TR("AGAIN?", "שוב?")
                                   : TR("NEW", "חדש"),
                  rgb(250, 238, 210));
    }
}
static void rnd_draw_end(void)
{
    const int d = gfx_w;
    if (s_t > 1.5f) {
        fb_dark(0, 0, d, d * 36 / 100, 2);
        text_wrap(&KF_NAME, d / 2, d * 15 / 100, d * 62 / 100,
                  TR("Grandma opens her eyes. \"The wood turns again. Thank you, my dear.\"",
                     "סבתא פותחת את עיניה. \"היער מסתובב שוב. תודה, יקירי.\""),
                  rgb(255, 244, 220), 26);
    }
    if (s_t > 3.0f) {
        fb_dark(0, d * 80 / 100, d, d * 20 / 100, 3);
        ui_text_c(&KF_TITLE, d / 2, d * 89 / 100, TR("THE END", "הסוף"), rgb(255, 230, 170));
    }
}
static void rnd_draw_nodata(void)
{
    const int d = gfx_w;
    gfx_fill(rgb(20, 16, 12));
    ui_ring(d / 2, d / 2, d / 2 - 3, 2, rgb(110, 86, 44), 255);
    ui_text_c(&KF_TITLE, d / 2, d * 30 / 100, TR("THE TURNING WOOD", "היער הסובב"), rgb(250, 238, 210));
    text_wrap(&KF_TEXT, d / 2, d * 45 / 100, d * 68 / 100,
              TR("The pictures of the game are not on the SD card. Install its data from the STORE.",
                 "תמונות המשחק אינן בכרטיס הזיכרון. התקינו את המידע שלו מהחנות."),
              rgb(255, 220, 180), 26);
}
static void rnd_key(int x, int y, int r)
{
    ui_circle(x, y, r, rgb(20, 16, 12), 210);
    ui_ring(x, y, r, 2, rgb(201, 162, 79), 230);
    ui_text_c(&KF_NAME, x, y + KF_NAME.asc / 2 - 1, "X", rgb(250, 230, 190));
}
static void rnd_render(void)
{
    if (s_mode == M_NODATA) {
        rnd_draw_nodata();
        return;
    }
    rnd_use();
    if (!rv_enter(&s_rv, true)) {
        gfx_fill(rgb(0, 0, 0));
        return;
    }
    switch (s_mode) {
    case M_TITLE:
    case M_END:
        for (int y = 0; y < SH; y++) {
            memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
        }
        if (s_mode == M_END && s_t < 1.5f) {
            fb_dark(0, 0, SW, SH, 2 + (int)(s_t * 4));  // (the light comes up)
        }
        break;
    case M_JOURNAL:
        draw_journal();
        break;
    case M_MAP:
        draw_map();
        break;
    case M_CU:
        for (int y = 0; y < SH; y++) {
            memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
        }
        cu_draw();
        if (s_solved_t > 0) {
            ui_circle(SW / 2, SH / 2, (int)(120 * (1.2f - s_solved_t)), rgb(255, 240, 200), (int)(160 * s_solved_t));
        }
        break;
    default:
        draw_place();
        if (s_shake > 0) {
            const int dx = (int)(fsin(s_shake * 60) * 2);
            if (dx) {
                for (int y = 0; y < SH; y++) {
                    memmove(g_fb + y * SW + (dx > 0 ? dx : 0), g_fb + y * SW + (dx > 0 ? 0 : -dx),
                            (size_t)(SW - abs(dx)) * 2);
                }
            }
        }
        exit_signs();
        draw_feedback();
        fade_frame(s_fade);
        break;
    }
    rv_leave(&s_rv);
    rv_out(&s_rv, s_mode == M_MAP ? 0 : gfx_w / 9);
    int bx, by, br;
    switch (s_mode) {
    case M_TITLE:
        rnd_draw_title();
        break;
    case M_END:
        rnd_draw_end();
        break;
    case M_JOURNAL:
    case M_MAP:
    case M_CU:
        rnd_x_xy(&bx, &by, &br);
        rnd_key(bx, by, br);
        break;
    case M_PLACE:
        if (s_bag) {
            draw_bag();
        } else {
            draw_msg();
        }
        if (s_fade < 0.5f) {
            draw_buttons();
        }
        break;
    default:
        break;
    }
}
#endif
