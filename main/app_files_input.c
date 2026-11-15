// FILES: the touch input of each page and the wheel of the Watcher (main/app_files.c has the rest).
#include "app_files_priv.h"

// ---------------------------------------------------------------- input

static int list_at(int x, int y)
{
    if (y >= HEAD_H && fil_list_busy()) {
        return CT_EMPTY;  // (the list is not complete: no row)
    }
#if BOARD_ROUND
    if (y >= LIST_B) {
        return y >= BAR_Y - 6 ? (x < SW / 2 ? CT_UP : CT_MORE) : CT_EMPTY;  // (UP and MORE: the bottom cap)
    }
#endif
    if (y < HEAD_H) {
        if (x < UP_W + 6) {
            return CT_UP;
        }
        if (x >= MORE_X - 4) {
            return CT_MORE;
        }
        return CT_UP;  // (the path line: up)
    }
    int i = (int)floorf((y - LIST_Y + fil_scroll) / ROW_H);
    if (i >= 0 && i < fil_n) {
        int ry = LIST_Y + i * ROW_H - (int)lroundf(fil_scroll);
        if (y >= ry && y < ry + ROW_H) {
            return i;
        }
    }
    return CT_EMPTY;
}

static int home_at(int x, int y)
{
#if BOARD_ROUND
    if (y >= BAR_Y - 6) {
        return CT_TOOLS;  // (CARD TOOLS: the bottom cap)
    }
    if (ui_hit(x, y, TILE1_X, TILE_Y, TILE_W, TILE_H, 0)) {
        return CT_CARD;
    }
    return ui_hit(x, y, TILE2_X, TILE2_Y, TILE_W, TILE_H, 0) ? CT_BOARD : -1;
#endif
    if (y < HOME_H && x >= TOOLS_X - 4) {
        return CT_TOOLS;
    }
    if (ui_hit(x, y, 6, TILE_Y, TILE_W, TILE_H, 0)) {
        return CT_CARD;
    }
    if (ui_hit(x, y, TILE2_X, TILE2_Y, TILE_W, TILE_H, 0)) {
        return CT_BOARD;
    }
    return -1;
}

static void list_input(const input_event_t *ev)
{
#if T5P
    fil_t5_list_input(ev);
    return;
#endif
    switch (ev->kind) {
    case IN_DOWN: {
        fil_touch = true;
        fil_drag = false;
        fil_x0 = ev->x;
        fil_y0 = ev->y;
        // a touch on the list goes to the scroll; a touch that stops a glide lights no row and gives no tap
        bool caught = ev->y >= LIST_Y && ks_down(&fil_ks, ev->y, ev->t_ms);
        fil_hot = caught ? -1 : list_at(ev->x, ev->y);
        break;
    }
    case IN_MOVE:
        if (fil_y0 >= LIST_Y && ks_move(&fil_ks, ev->y, ev->t_ms)) {
            fil_drag = true;
            input_tap_cancel();
            fil_hot = -1;
        }
        if (fil_drag) {
            fil_scroll = ks_pos(&fil_ks);
        } else if (abs(ev->x - fil_x0) > DRAG_PX || abs(ev->y - fil_y0) > DRAG_PX) {
            if (list_at(ev->x, ev->y) != fil_hot) {
                fil_hot = -1;
            }
        }
        break;
    case IN_UP:
        fil_touch = false;
        ks_up(&fil_ks, ev->t_ms);
        fil_drag = false;
        fil_hot = -1;
        break;
    case IN_TAP: {
        if (fil_y0 >= LIST_Y && !ks_tap_ok(&fil_ks)) {
            break;
        }
        int c = list_at(ev->x, ev->y);
        if (c == CT_UP) {
            fil_go_up();
        } else if (c == CT_MORE) {
            fil_panel_open_more();
        } else if (c >= 0 && c < fil_n) {
            fil_open_entry(c, ev->t_ms);
        }
        break;
    }
    case IN_LONG: {
        if (fil_y0 >= LIST_Y && !ks_tap_ok(&fil_ks)) {
            break;
        }
        int c = list_at(ev->x, ev->y);
        fil_hot = -1;
        if (c >= 0 && c < fil_n) {
            fil_panel_open_entry(c);
        } else if (c == CT_EMPTY) {
            fil_panel_open_more();
        }
        break;
    }
    default:
        break;
    }
}

static void home_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        fil_hot = home_at(ev->x, ev->y);
    } else if (ev->kind == IN_MOVE) {
        if (home_at(ev->x, ev->y) != fil_hot) {
            fil_hot = -1;
        }
    } else if (ev->kind == IN_UP) {
        fil_hot = -1;
    } else if (ev->kind == IN_LONG && home_at(ev->x, ev->y) == CT_CARD) {
        fil_panel_open_tools();
    } else if (ev->kind == IN_TAP) {
        int c = home_at(ev->x, ev->y);
        if (c == CT_TOOLS) {
            fil_panel_open_tools();
        } else if (c == CT_BOARD) {
            if (fil_data_ok) {
                fil_open_root(RT_BOARD);
                sfx_ui(SFX_CLICK);
            } else {
                fil_say("THE BOARD FILES ARE NOT THERE", true, ev->t_ms);
            }
        } else if (c == CT_CARD) {
            if (!fil_card_ok) {
                fil_card_look();
                fil_space_read();
            }
            if (fil_card_ok) {
                fil_open_root(RT_CARD);
                sfx_ui(SFX_CLICK);
            } else {
                fil_say("NO CARD. PUT IN A CARD, THEN TAP AGAIN", true, ev->t_ms);
            }
        }
    }
}

static void act_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        fil_hot = fil_act_at(ev->x, ev->y);
    } else if (ev->kind == IN_MOVE) {
        if (fil_act_at(ev->x, ev->y) != fil_hot) {
            fil_hot = -1;
        }
    } else if (ev->kind == IN_UP) {
        fil_hot = -1;
    } else if (ev->kind == IN_TAP) {
        int c = fil_act_at(ev->x, ev->y);
        if (c == CT_CLOSE) {
            sfx_ui(SFX_BACK);
            if (fil_pkind == PK_TOOLS && fil_root < 0) {
                fil_page = P_HOME;
            } else {
                fil_page_list();
            }
        } else if (c >= 0 && c < fil_nact) {
            fil_do_act(fil_act[c], ev->t_ms);
        }
    }
}

static void ask_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        fil_qguard = ev->t_ms - fil_qopen_t < ASK_GUARD_MS;  // (a bounce, a double tap: this touch does nothing)
        fil_qhot = fil_qguard ? -1 : fil_ask_at(ev->x, ev->y);
        fil_qhold_t0 = (fil_qhot == 1 && fil_qhold_ms) ? ev->t_ms : -1;
        return;
    }
    if (fil_qguard) {
        return;
    }
    if (ev->kind == IN_MOVE) {
        if (fil_ask_at(ev->x, ev->y) != fil_qhot) {
            fil_qhot = -1;
            fil_qhold_t0 = -1;
        }
    } else if (ev->kind == IN_UP) {
        if (fil_qhold_t0 >= 0) {
            sfx_ui(SFX_BACK);  // (let go before the end of the hold)
        }
        fil_qhot = -1;
        fil_qhold_t0 = -1;
    } else if (ev->kind == IN_TAP) {
        int a = fil_ask_at(ev->x, ev->y);
        if (a == 1 && fil_qhold_ms) {
            return;  // (YES needs the hold)
        }
        if (a >= 0) {
            fil_ask_answer(a, ev->t_ms);
        }
    }
}

static int view_back_at(int x, int y)
{
#if BOARD_ROUND
    return (y < TXT_TOP || (y >= BAR_Y - 6 && abs(x - SW / 2) < FR(90))) ? CT_CLOSE : -1;  // (the title, BACK)
#endif
    return (y < TXT_TOP && x < UP_W + 6) ? CT_CLOSE : -1;
}

static void text_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        fil_touch = true;
        fil_drag = false;
        fil_y0 = ev->y;
        fil_x0 = ev->x;
        if (ev->y >= TXT_TOP) {
            ks_down(&fil_kt, ev->y, ev->t_ms);
        }
        fil_hot = view_back_at(ev->x, ev->y);
        break;
    case IN_MOVE:
        if (fil_y0 >= TXT_TOP && ks_move(&fil_kt, ev->y, ev->t_ms)) {
            fil_drag = true;
            input_tap_cancel();
            fil_hot = -1;
        }
        if (fil_drag) {
            fil_tscroll = ks_pos(&fil_kt);
        } else if (view_back_at(ev->x, ev->y) != fil_hot) {
            fil_hot = -1;
        }
        break;
    case IN_UP:
        fil_touch = false;
        ks_up(&fil_kt, ev->t_ms);
        fil_drag = false;
        fil_hot = -1;
        break;
    case IN_TAP:
        if (view_back_at(ev->x, ev->y) == CT_CLOSE) {
            fil_text_free();
            sfx_ui(SFX_BACK);
            fil_page_list();
        }
        break;
    default:
        break;
    }
}

static void pic_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        fil_hot = (fil_pbar_until > ev->t_ms) ? view_back_at(ev->x, ev->y) : -1;
    } else if (ev->kind == IN_UP) {
        fil_hot = -1;
    } else if (ev->kind == IN_TAP) {
        if (fil_pbar_until > ev->t_ms && view_back_at(ev->x, ev->y) == CT_CLOSE) {
            fil_pic_free();
            sfx_ui(SFX_BACK);
            fil_page_list();
            return;
        }
        fil_pbar_until = fil_pbar_until > ev->t_ms ? 0 : ev->t_ms + 4000;  // a tap shows or hides the bar
    }
}

static void info_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        fil_hot = view_back_at(ev->x, ev->y);
    } else if (ev->kind == IN_UP) {
        fil_hot = -1;
    } else if (ev->kind == IN_TAP && view_back_at(ev->x, ev->y) == CT_CLOSE) {
        sfx_ui(SFX_BACK);
        fil_page_list();
    }
}

static void job_input(const input_event_t *ev)
{
#if BOARD_ROUND
    bool on = fil_job.kind != J_FORMAT && ev->y >= BAR_Y - 6 && abs(ev->x - SW / 2) < FR(90);  // (the bottom cap)
#else
    bool on = fil_job.kind != J_FORMAT && ui_hit(ev->x, ev->y, STOP_X, SH - STOP_H - 6, STOP_W, STOP_H + 6, 0);
#endif
    if (ev->kind == IN_DOWN) {
        fil_hot = on ? CT_STOP : -1;
    } else if (ev->kind == IN_UP) {
        fil_hot = -1;
    } else if (ev->kind == IN_TAP && on) {
        fil_job_stop(ev->t_ms);
    }
}

void fil_input(const input_event_t *ev)
{
    switch (fil_page) {
    case P_HOME:
        home_input(ev);
        break;
    case P_LIST:
        list_input(ev);
        break;
    case P_ACT:
        act_input(ev);
        break;
    case P_ASK:
        ask_input(ev);
        break;
    case P_KBD:
        fil_kbd_done(kbd_input(ev), ev->t_ms);
        break;
    case P_INFO:
        info_input(ev);
        break;
    case P_TEXT:
        text_input(ev);
        break;
    case P_PIC:
        pic_input(ev);
        break;
    case P_JOB:
        job_input(ev);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- the wheel (the Watcher)

#if BOARD_WHEEL
void fil_wheel(int st, int pr, int64_t now)
{
    switch (fil_page) {
    case P_HOME:
        if (st) {
            fil_focus = fil_focus < 0 ? 0 : (fil_focus + (st > 0 ? 1 : 2)) % 3;
        }
        if (pr && fil_focus >= 0) {
            int c = fil_focus == 0 ? CT_CARD : fil_focus == 1 ? CT_BOARD : CT_TOOLS;
            int x = c == CT_TOOLS  ? TOOLS_X + 10
                    : c == CT_CARD ? 20
                                   : TILE2_X + 20,
                y = c == CT_TOOLS  ? 10
                    : c == CT_CARD ? TILE_Y + 10
                                   : TILE2_Y + 10;
            input_event_t e = {.kind = IN_TAP, .x = (int16_t)x, .y = (int16_t)y, .t_ms = now};
            home_input(&e);
        }
        break;
    case P_LIST:
        if (st && fil_n > 0) {
            int f = fil_focus < 0 ? (int)(fil_scroll / ROW_H) : fil_focus + st;
            fil_focus = f < 0 ? 0 : f >= fil_n ? fil_n - 1 : f;
            float top = (float)(fil_focus * ROW_H), g = ks_goal(&fil_ks);
            if (top < g) {
                ks_to(&fil_ks, top);
            } else if (top + ROW_H > g + LST_H) {
                ks_to(&fil_ks, top + ROW_H - LST_H);
            }
            sfx_ui(SFX_TICK);
        }
        if (pr && fil_focus >= 0 && fil_focus < fil_n && !fil_list_busy()) {
            fil_open_entry(fil_focus, now);
        }
        break;
    case P_ACT:
        if (st && fil_nact > 0) {
            fil_focus = fil_focus < 0 ? 0 : ((fil_focus + st) % fil_nact + fil_nact) % fil_nact;
        }
        if (pr && fil_focus >= 0 && fil_focus < fil_nact) {
            fil_do_act(fil_act[fil_focus], now);
        }
        break;
    case P_ASK:
        if (st) {
            fil_qfocus = fil_qfocus < 0 ? 0 : 1 - fil_qfocus;
            if (!fil_qa[fil_qfocus]) {
                fil_qfocus = 1 - fil_qfocus;
            }
            fil_qarmed = false;
        }
        if (pr && fil_qfocus >= 0) {
            if (fil_qfocus == 1 && fil_qhold_ms && !fil_qarmed) {
                fil_qarmed = true;  // (a hold: the second push acts)
            } else {
                fil_ask_answer(fil_qfocus, now);
            }
        }
        break;
    case P_KBD:
        fil_kbd_done(kbd_wheel(st, pr), now);
        break;
    case P_TEXT:
        if (st) {
            ks_nudge(&fil_kt, (float)(st * TXT_LH * 2));
        }
        if (pr) {
            fil_text_free();
            fil_page_list();
        }
        break;
    case P_PIC:
    case P_INFO:
        if (pr) {
            fil_pic_free();
            fil_page_list();
        }
        break;
    default:
        break;
    }
}
#endif
