#pragma once

// The WALKIE and FRIENDS part of the board web page (http://<board>/walkie, in the menu of the
// board page): LISTEN (the microphone of this board in the browser), HOLD TO TALK (the browser
// microphone to this board), the WALKIE switches, the friends. The APIs (with CORS) are for the
// central manager on the PC (manager/): /api/boards, /api/walkie, /api/friends, /ws/audio.

#include <stdbool.h>

void web_walkie_register(void *h);  // h: the httpd_handle_t of wifi.c
// A browser listens to this board or talked in the last 2 s (main.c: keep the speaker and the
// radio on while the screen is off).
bool web_audio_active(void);
// main.c, after the app draws: a "WEB LISTENS" bar while a browser listens (the board shows it).
void web_walkie_overlay(void);
