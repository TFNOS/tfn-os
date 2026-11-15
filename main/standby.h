#pragma once
// STANDBY: the calm screen of the moment (the time, the real sky of the place, what is next). It shows at each wake
// of the screen and after one minute with no touch in the MENU, never over an open app. A tap or a swipe up opens the
// menu where it was; a tap on a slot of GLANCE opens the app of the slot. It is a layer over the menu (the menu stays
// the app: the power rules of power.c see the menu), and it draws at a low frame rate (main.c skips the other frames).
// The settings: SETUP > STANDBY (standby_page.h). The files: standby.c (the state, the touches, the console),
// standby_data.c (the place, the weather service, the Hebrew date, the sun, the NEXT item), standby_draw.c (the faces
// and the layouts of each frame), standby_sky.c (the live sky).
#include <stdbool.h>
#include <stdint.h>
#include "input.h"

void standby_init(void);  // main.c, after the notices: the settings and the last weather
// main.c, each frame: the minute in the menu (screen: 2 on, 1 dim, 0 off; hand_at: the last touch or key), the
// weather service
void standby_tick(int64_t now, int screen, int64_t hand_at);
void standby_wake(int64_t now);               // main_saver.c: the screen comes on (it shows when the menu is the app)
bool standby_on(void);                        // it shows now (the menu is the app)
void standby_show(int64_t now);               // show it now (the console, the SETUP page)
void standby_hide(void);                      // the menu again (a tap, the PWR key, the panel, an app opens)
bool standby_input(const input_event_t *ev);  // main.c: true = STANDBY took the touch
void standby_draw(int64_t now);               // main.c: the whole frame
bool standby_skip(int64_t now);               // main.c: true = no new frame is due now (the LCD keeps the last one)
void standby_console(char *args);             // the console "standby": the state; show, hide, fetch, face <n>, on <0|1>
