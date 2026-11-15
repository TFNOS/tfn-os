#pragma once

// THE THEME: the colours of the system screens in one place (the menu, the settings panel, SETUP, the notices, the
// keyboard, STORE, FILES, FRIENDS, the calibration, the lobby of 2 boards). The code of a screen uses these names, not
// colour numbers; a new look of the system is a change of this file. A name is THEME_<SCREEN>_<ROLE>; its value is an
// RGB565 colour of rgb() (tfn/gfx.h). The pictures of an app (art, icons, game drawings) keep their own colours.
// An app can use these names too, to look like the system.
#include "gfx.h"

// ------ the menu and the launcher (main.c, app_menu.c, layout.c, menu_*.c)
// the menu of all boards: the frame, the words, the marks of an item, the message of a change
#define THEME_MENU_BG           rgb(16, 14, 36)     // the background when the room picture has no memory
#define THEME_MENU_TEXT         rgb(255, 255, 255)  // the name of the middle item, the title, the place bar
#define THEME_MENU_TEXT_SOFT    rgb(200, 200, 215)  // the index, the count, the help lines
#define THEME_MENU_SHADOW       rgb(0, 0, 0)        // the soft shadow under an item
#define THEME_MENU_GLINT        rgb(255, 255, 255)  // the glint on the glass of the middle item
#define THEME_MENU_FLOOR        rgb(255, 255, 255)  // the faint floor line of the room
#define THEME_MENU_TILE_EDGE    rgb(70, 84, 130)    // the frame of a group tile
#define THEME_MENU_TILE_PRESSED rgb(250, 215, 30)   // the frame of a group tile under the finger
#define THEME_MENU_TILE_COUNT   rgb(255, 255, 255)  // the count of the apps on a group tile
#define THEME_MENU_FAV          rgb(255, 214, 40)   // the star of a favorite
#define THEME_MENU_FAV_EDGE     rgb(40, 30, 10)     // the dark edge of the star of a favorite
#define THEME_MENU_HOLD_TRACK   rgb(30, 30, 50)     // the hold bar under an item: its track
#define THEME_MENU_HOLD_FILL    rgb(250, 215, 30)   // the hold bar under an item: the part that fills
#define THEME_MENU_PILL         rgb(255, 207, 90)   // the NEW pill and the NEW mark
#define THEME_MENU_PILL_TEXT    rgb(26, 18, 6)      // the words on the NEW pill
#define THEME_MENU_POP_EDGE     rgb(255, 207, 90)   // the message of a change: its edge
#define THEME_MENU_POP_BG       rgb(24, 22, 52)     // the message of a change: its box
#define THEME_MENU_POP_TEXT     rgb(255, 238, 160)  // the message of a change: its words
// the status: the WiFi signal, the battery, the badge of the unread notices
#define THEME_MENU_WIFI_ON    rgb(200, 205, 225)  // a bar of the WiFi signal that is lit
#define THEME_MENU_WIFI_OFF   rgb(60, 66, 100)    // a bar of the WiFi signal that is not lit
#define THEME_MENU_BAT_EDGE   rgb(200, 205, 225)  // the edge of the battery and its percent
#define THEME_MENU_BAT_PLATE  rgb(8, 10, 28)      // the dark plate under the battery
#define THEME_MENU_BAT_OK     rgb(90, 220, 110)   // the battery level: more than 40 percent
#define THEME_MENU_BAT_WARN   rgb(250, 200, 40)   // the battery level: 16 to 40 percent
#define THEME_MENU_BAT_LOW    rgb(240, 70, 60)    // the battery level: 15 percent or less
#define THEME_MENU_BAT_BOLT   rgb(255, 240, 120)  // the bolt on the battery while it charges
#define THEME_MENU_BADGE_EDGE rgb(16, 20, 32)     // the badge of the unread notices: its dark edge
#define THEME_MENU_BADGE_BG   rgb(226, 70, 59)    // the badge of the unread notices: the red pill
#define THEME_MENU_BADGE_TEXT rgb(255, 255, 255)  // the badge of the unread notices: the count
// EDIT HOME and the sheet of an item (menu_home_draw.c)
#define THEME_MENU_HM_BTN          rgb(64, 62, 110)    // a button
#define THEME_MENU_HM_BTN_HOT      rgb(120, 120, 190)  // a button under the finger
#define THEME_MENU_HM_BTN_RED      rgb(176, 52, 60)    // a button that removes (DELETE, TAKE APART)
#define THEME_MENU_HM_BTN_RED_HOT  rgb(230, 90, 90)    // a button that removes, under the finger
#define THEME_MENU_HM_BTN_DONE     rgb(54, 150, 84)    // the DONE button
#define THEME_MENU_HM_BTN_DONE_HOT rgb(120, 220, 140)  // the DONE button under the finger
#define THEME_MENU_HM_BTN_OFF      rgb(46, 46, 60)     // a button that is off
#define THEME_MENU_HM_BTN_TEXT     rgb(255, 255, 255)  // the words of a button
#define THEME_MENU_HM_BTN_TEXT_OFF rgb(140, 140, 150)  // the words of a button that is off
#define THEME_MENU_HM_LOCK         rgb(220, 220, 230)  // the small lock on a button that the manager keeps
#define THEME_MENU_HM_FOCUS        rgb(255, 214, 40)   // the bars of the key focus
#define THEME_MENU_HM_X            rgb(70, 70, 96)     // the close button: its circle
#define THEME_MENU_HM_X_MARK       rgb(255, 255, 255)  // the close button: its X
#define THEME_MENU_HM_SHOW         rgb(44, 44, 64)     // the tile of the hidden apps
#define THEME_MENU_HM_LIFT         rgb(255, 214, 40)   // the ring of the lifted tile
#define THEME_MENU_HM_SLOT         rgb(255, 255, 255)  // the faint box at the old place of the dragged tile
#define THEME_MENU_HM_MERGE        rgb(255, 214, 40)   // the glow of a tile that makes a group with the dragged tile
#define THEME_MENU_HM_DROP         rgb(255, 214, 40)   // the bar at the place of the drop
#define THEME_MENU_HM_HINT         rgb(255, 214, 40)   // the words EDIT HOME and TAP A PLACE
#define THEME_MENU_HM_TEXT_SOFT    rgb(210, 210, 225)  // the level and the page of EDIT HOME
// the round menu (menu_round.c)
#define THEME_MENU_ROUND_ACCENT    rgb(250, 215, 30)   // the ring of the selected tile, the place dot, the message edge
#define THEME_MENU_ROUND_TEXT      rgb(240, 244, 255)  // the time, the name of the selected item
#define THEME_MENU_ROUND_TEXT_SOFT rgb(170, 180, 220)  // where BACK goes
#define THEME_MENU_ROUND_EMPTY     rgb(235, 240, 255)  // NO APPS
#define THEME_MENU_ROUND_RIM       rgb(70, 80, 120)    // the thin arc of the place (many items)
#define THEME_MENU_ROUND_DOT       rgb(110, 120, 170)  // the dot of an item that is not selected
#define THEME_MENU_ROUND_HELP      rgb(120, 130, 170)  // the help line at the bottom
#define THEME_MENU_ROUND_POP_TEXT  rgb(255, 238, 140)  // the words of the message of a change
// the menu of the tablet (menu_tab5_draw.c)
#define THEME_MENU_TB_SHADOW    rgb(10, 10, 24)     // the dark shadow of a text
#define THEME_MENU_TB_RAIL_EDGE rgb(255, 255, 255)  // the edge line of the rail
#define THEME_MENU_TB_TAB_HL    rgb(255, 255, 255)  // the highlight of the tab now
#define THEME_MENU_TB_TAB_TEXT  rgb(190, 194, 214)  // the name of a tab that is not selected
#define THEME_MENU_TB_COUNT     rgb(200, 204, 222)  // the count of the apps next to the title
#define THEME_MENU_TB_BACK      rgb(255, 255, 255)  // the BACK button: its box, its arrow, its words
#define THEME_MENU_TB_EMPTY     rgb(230, 232, 245)  // NO APPS
#define THEME_MENU_TB_FOCUS     rgb(120, 220, 255)  // the ring of the key focus
#define THEME_MENU_TB_FOCUS_IN  rgb(255, 255, 255)  // the thin inner ring of the key focus
#define THEME_MENU_TB_SCROLL    rgb(255, 255, 255)  // the scroll bar
#define THEME_MENU_TB_NEW_EDGE  rgb(10, 10, 24)     // the dark edge of the NEW mark
#define THEME_MENU_TB_BAT_EDGE  rgb(220, 224, 240)  // the edge of the battery and its percent
#define THEME_MENU_TB_BAT_PLATE rgb(16, 18, 34)     // the box of the battery
#define THEME_MENU_TB_WIFI_ON   rgb(220, 224, 240)  // a bar of the WiFi signal that is lit
#define THEME_MENU_TB_WIFI_OFF  rgb(80, 86, 120)    // a bar of the WiFi signal that is not lit
// the grid menu of a small portrait panel (menu_grid.c, BOARD_UI_GRID: the AMOLED-1.8): soft colours on black
#define THEME_MENU_GR_BG       rgb(0, 0, 0)        // the black of the AMOLED (under the soft light of the group)
#define THEME_MENU_GR_TEXT     rgb(244, 245, 250)  // the time, the title, the names of the icons
#define THEME_MENU_GR_SOFT     rgb(160, 164, 184)  // the count of the apps, the percent of the battery, the hints
#define THEME_MENU_GR_ICON     rgb(236, 238, 246)  // the WiFi bars, the battery edge, the arrow of BACK
#define THEME_MENU_GR_DIM      rgb(70, 74, 92)     // the WiFi bars that are not lit
#define THEME_MENU_GR_BACK     rgb(255, 255, 255)  // the round plate of BACK (soft: a low alpha)
#define THEME_MENU_GR_NOTE     rgb(255, 170, 70)   // the pill of the unread notices in the status bar
#define THEME_MENU_GR_NOTE_TXT rgb(30, 18, 6)      // the count on that pill
#define THEME_MENU_GR_SCROLL   rgb(255, 255, 255)  // the scroll bar
// the change of the flash layout (layout.c): the screen while the board writes the new table
#define THEME_LAYOUT_BG    rgb(10, 14, 36)     // the background
#define THEME_LAYOUT_TITLE rgb(255, 200, 80)   // the title
#define THEME_LAYOUT_TEXT  rgb(230, 232, 245)  // the line of the step
#define THEME_LAYOUT_TRACK rgb(40, 46, 80)     // the progress bar: its track
#define THEME_LAYOUT_BAR   rgb(120, 230, 150)  // the progress bar: the part that is done
#define THEME_LAYOUT_WARN  rgb(255, 120, 120)  // DO NOT TURN THE BOARD OFF
// the recovery screen of the device guard (main.c)
#define THEME_RECOVERY_BG    rgb(8, 12, 24)      // the background
#define THEME_RECOVERY_TITLE rgb(255, 190, 90)   // the title
#define THEME_RECOVERY_TEXT  rgb(230, 230, 240)  // the line under the title

// ------ the system UI: the settings panel, SETUP, the notices, the keyboard (sysui*.c, app_settings*.c, notify*.c,
// kbd.c, uikit.c)
// the settings panel (sysui_panel.c): the panel, the sliders, RESUME and MENU, the chips, the tab and the grip
#define THEME_PANEL_BACK             rgb(8, 8, 16)       // under the panel, when there is no picture of the app
#define THEME_PANEL_BG               rgb(20, 22, 38)     // the panel
#define THEME_PANEL_EDGE             rgb(60, 66, 100)    // the line at the bottom edge of the panel
#define THEME_PANEL_TEXT             rgb(236, 238, 248)  // the text
#define THEME_PANEL_TEXT_DIM         rgb(150, 156, 190)  // the dim text, the handle, a muted slider
#define THEME_PANEL_TRACK            rgb(44, 48, 78)     // the track of a slider
#define THEME_PANEL_FOCUS            rgb(255, 214, 90)   // the ring of the control with the focus
#define THEME_PANEL_FOCUS_ADJUST     rgb(120, 255, 170)  // the ring while the keys change the control
#define THEME_PANEL_APP_NAME         rgb(240, 196, 112)  // the name of the app
#define THEME_PANEL_VOL              rgb(90, 200, 255)   // the VOLUME slider and its value
#define THEME_PANEL_BRI              rgb(255, 196, 80)   // the BRIGHTNESS slider and its value
#define THEME_PANEL_BOOST            rgb(255, 150, 40)   // the VOLUME over 100 (the boost of the mix)
#define THEME_PANEL_KNOB             rgb(255, 255, 255)  // the knob of a slider
#define THEME_PANEL_RESUME           rgb(30, 140, 75)    // RESUME
#define THEME_PANEL_RESUME_DOWN      rgb(60, 190, 110)   // RESUME under a finger
#define THEME_PANEL_RESUME_EDGE      rgb(140, 255, 190)  // the edge of RESUME
#define THEME_PANEL_RESUME_TEXT      rgb(255, 255, 255)  // the text of RESUME
#define THEME_PANEL_BTN              rgb(40, 44, 72)     // MENU
#define THEME_PANEL_BTN_DOWN         rgb(70, 76, 120)    // MENU under a finger
#define THEME_PANEL_BTN_EDGE         rgb(100, 108, 150)  // the edge of MENU
#define THEME_PANEL_HOLD             rgb(150, 80, 40)    // the fill while a finger holds MENU
#define THEME_PANEL_CHIP             rgb(40, 44, 72)     // a chip
#define THEME_PANEL_CHIP_DOWN        rgb(70, 76, 120)    // a chip under a finger
#define THEME_PANEL_CHIP_EDGE        rgb(100, 108, 150)  // the edge of a chip
#define THEME_PANEL_CHIP_UNREAD_EDGE rgb(240, 170, 70)   // the edge of the NOTICES chip with unread notices
#define THEME_PANEL_CHIP_LOSS_EDGE   rgb(196, 120, 104)  // the edge of a chip that can lose data (a hold acts)
#define THEME_PANEL_CHIP_LOSS_TEXT   rgb(255, 214, 196)  // the text of that chip
#define THEME_PANEL_CHIP_ARMED       rgb(112, 38, 26)    // that chip after a short tap (HOLD)
#define THEME_PANEL_CHIP_ARMED_EDGE  rgb(255, 140, 100)  // the edge of the armed chip
#define THEME_PANEL_CHIP_ARMED_TOP   rgb(255, 200, 170)  // the small top line of the armed chip
#define THEME_PANEL_CHIP_ARMED_TEXT  rgb(255, 230, 210)  // the name on the armed chip
#define THEME_PANEL_CHIP_HOLD        rgb(150, 70, 40)    // the fill while a finger holds that chip
#define THEME_PANEL_TAB_EDGE         rgb(140, 150, 200)  // the edge of the tab of the two-step pull
#define THEME_PANEL_TAB_HANDLE       rgb(255, 255, 255)  // the handle on the tab
#define THEME_PANEL_TAB_ARROW        rgb(255, 214, 90)   // the arrow on the tab
#define THEME_PANEL_GRIP             rgb(255, 255, 255)  // the grip at the top of the screen
#define THEME_PANEL_GRIP_EDGE        rgb(0, 0, 0)        // the dark edge of the grip (it shows on a light game)
// the soft look of the settings panel (BOARD_UI_GRID: the AMOLED-1.8)
#define THEME_PANEL_GR_BG     rgb(16, 16, 22)   // the card of the panel (near the black of the AMOLED)
#define THEME_PANEL_GR_SHADE  rgb(0, 0, 0)      // the soft shade under the card
#define THEME_PANEL_GR_BTN    rgb(40, 42, 54)   // MENU and the chips (a round pill, no edge)
#define THEME_PANEL_GR_RESUME rgb(40, 150, 92)  // RESUME (a round pill, no edge)

// the question of a call (sysui.c ask_draw): ANSWER and the other key
#define THEME_ASK_BACK       rgb(8, 8, 16)       // under the box, when there is no picture of the app
#define THEME_ASK_BG         rgb(22, 22, 40)     // the box
#define THEME_ASK_EDGE       rgb(90, 220, 150)   // the edge of the box
#define THEME_ASK_INITIAL    rgb(24, 22, 40)     // the first letter in the circle of the caller
#define THEME_ASK_NAME       rgb(255, 248, 230)  // the name
#define THEME_ASK_LINE       rgb(170, 230, 200)  // the line under the name
#define THEME_ASK_WARN       rgb(255, 170, 80)   // the note of a tap that cannot answer (not by a finger)
#define THEME_ASK_YES        rgb(30, 140, 75)    // ANSWER (the glow goes from this colour to the next)
#define THEME_ASK_YES_GLOW   rgb(50, 200, 110)   // ANSWER at the top of its glow
#define THEME_ASK_YES_EDGE   rgb(140, 255, 190)  // the edge of ANSWER
#define THEME_ASK_YES_TEXT   rgb(255, 255, 255)  // the text of ANSWER
#define THEME_ASK_NO         rgb(60, 32, 44)     // the other key
#define THEME_ASK_NO_EDGE    rgb(210, 100, 120)  // the edge of the other key
#define THEME_ASK_NO_TEXT    rgb(255, 210, 215)  // the text of the other key
#define THEME_ASK_CLOSE      rgb(52, 52, 84)     // the circle of the close mark
#define THEME_ASK_CLOSE_MARK rgb(220, 220, 240)  // the X of the close mark

// FIND THE BOARD (sysui.c): the screen flashes between two looks; _HI = the light look
#define THEME_FIND_BG      rgb(30, 10, 10)     // the screen
#define THEME_FIND_BG_HI   rgb(255, 80, 30)    // the screen, light
#define THEME_FIND_TEXT    rgb(255, 240, 200)  // HERE!
#define THEME_FIND_TEXT_HI rgb(30, 10, 10)     // HERE!, light
#define THEME_FIND_HINT    rgb(200, 180, 160)  // TAP TO STOP
#define THEME_FIND_HINT_HI rgb(60, 20, 10)     // TAP TO STOP, light

// a message of the system (sysui.c sysui_message)
#define THEME_MSG_BACK rgb(8, 8, 16)       // under the box, when there is no picture of the app
#define THEME_MSG_BG   rgb(20, 18, 40)     // the box
#define THEME_MSG_EDGE rgb(240, 196, 112)  // the edge of the box
#define THEME_MSG_TEXT rgb(255, 248, 230)  // the text
#define THEME_MSG_HINT rgb(180, 170, 200)  // TAP TO CLOSE

// SETUP (app_settings*.c): the list, the WiFi pages and the QR page; app_settings.c copies the first 8 into set_c_*
#define THEME_SETUP_TEXT       rgb(235, 240, 255)  // the text
#define THEME_SETUP_TEXT_DIM   rgb(130, 140, 175)  // the dim text
#define THEME_SETUP_BOX        rgb(40, 48, 80)     // a button, a bar, a switch
#define THEME_SETUP_BOX_ON     rgb(50, 150, 80)    // a button or a switch that is on
#define THEME_SETUP_ACCENT     rgb(250, 215, 30)   // the control under a finger, the titles, the addresses
#define THEME_SETUP_CARD       rgb(20, 24, 46)     // a row of the list
#define THEME_SETUP_CARD2      rgb(24, 28, 54)     // the next row of the list, the track of the scroll bar
#define THEME_SETUP_LINE       rgb(70, 82, 130)    // the edges, the scroll bar
#define THEME_SETUP_BG         rgb(10, 12, 26)     // the screen
#define THEME_SETUP_TEXT_HOT   rgb(0, 0, 0)        // the text on the control under a finger
#define THEME_SETUP_BAR        rgb(70, 130, 230)   // the fill of a bar (VOLUME, BRIGHTNESS)
#define THEME_SETUP_BOOST      rgb(255, 150, 40)   // the VOLUME over 100 (the boost of the mix)
#define THEME_SETUP_KNOB       rgb(230, 235, 250)  // the knob of a switch
#define THEME_SETUP_BALL       rgb(200, 200, 215)  // the ball of the TILT TEST
#define THEME_SETUP_BALL_SHINE rgb(255, 255, 255)  // the light point on the ball
#define THEME_SETUP_TILT_FAULT rgb(240, 80, 80)    // NO IMU
#define THEME_SETUP_OK         rgb(90, 230, 120)   // a good state: WiFi on, the hotspot on, a web password
#define THEME_SETUP_WARN       rgb(255, 170, 80)   // a warning: no web password
#define THEME_SETUP_FAULT      rgb(255, 110, 110)  // a fault: the WiFi could not connect
#define THEME_SETUP_LEVEL_OK   rgb(90, 220, 110)   // TEMPERATURE and FREE MEMORY: good
#define THEME_SETUP_LEVEL_WARN rgb(250, 200, 40)   // TEMPERATURE and FREE MEMORY: near the limit
#define THEME_SETUP_LEVEL_HIGH rgb(240, 70, 60)    // TEMPERATURE and FREE MEMORY: at the limit
#define THEME_SETUP_ROW        rgb(26, 30, 56)     // a row of a WiFi list
#define THEME_SETUP_AUTO_ON    rgb(30, 110, 60)    // AUTO: ON of a saved network
#define THEME_SETUP_FORGET     rgb(90, 34, 48)     // FORGET of a saved network
#define THEME_SETUP_SIGNAL_ON  rgb(90, 220, 120)   // a bar of the WiFi signal that is on
#define THEME_SETUP_SIGNAL_OFF rgb(50, 56, 90)     // a bar of the WiFi signal that is off

// the NOTIFICATIONS page of SETUP (notify_page.c)
#define THEME_NOTE_PAGE_BG           rgb(10, 12, 26)     // the screen and the head
#define THEME_NOTE_PAGE_LINE         rgb(50, 60, 100)    // the line over the rows of the apps
#define THEME_NOTE_PAGE_TEXT         rgb(240, 244, 255)  // the name of a row
#define THEME_NOTE_PAGE_PILL         rgb(40, 44, 64)     // a choice
#define THEME_NOTE_PAGE_PILL_ON      rgb(36, 74, 150)    // the choice that is on
#define THEME_NOTE_PAGE_PILL_DOWN    rgb(80, 96, 150)    // a choice under a finger
#define THEME_NOTE_PAGE_PILL_EDGE    rgb(110, 130, 190)  // the lines of a choice
#define THEME_NOTE_PAGE_PILL_TEXT    rgb(170, 176, 200)  // the text of a choice
#define THEME_NOTE_PAGE_PILL_TEXT_ON rgb(255, 255, 255)  // the text of the choice that is on
#define THEME_NOTE_PAGE_SCROLL       rgb(200, 208, 230)  // the scroll bar
#define THEME_NOTE_PAGE_TITLE        rgb(120, 230, 255)  // NOTIFICATIONS
#define THEME_NOTE_PAGE_BACK         rgb(28, 24, 48)     // the BACK strip
#define THEME_NOTE_PAGE_BACK_TEXT    rgb(255, 255, 255)  // BACK

// the notices (notify.c): the center (the list) and the banner
#define THEME_NOTE_BG          rgb(12, 15, 24)     // the screen of the center
#define THEME_NOTE_EMPTY       rgb(140, 150, 170)  // NO NOTIFICATIONS
#define THEME_NOTE_CARD        rgb(28, 34, 48)     // a notice
#define THEME_NOTE_CARD_DOWN   rgb(44, 54, 78)     // a notice under a finger
#define THEME_NOTE_CARD_EDGE   rgb(58, 68, 92)     // the edge of a notice
#define THEME_NOTE_TEXT        rgb(236, 240, 248)  // the title of a notice, the head, the text of the head keys
#define THEME_NOTE_TEXT_DIM    rgb(150, 160, 185)  // the app and the age of a notice
#define THEME_NOTE_BODY        rgb(190, 198, 214)  // the text of a notice
#define THEME_NOTE_ACT         rgb(40, 50, 72)     // a key of a notice
#define THEME_NOTE_ACT_FIRST   rgb(42, 85, 201)    // the first key of a notice
#define THEME_NOTE_ACT_DOWN    rgb(80, 110, 200)   // a key of a notice under a finger
#define THEME_NOTE_ACT_EDGE    rgb(90, 110, 160)   // the edge of a key of a notice
#define THEME_NOTE_ACT_TEXT    rgb(240, 244, 250)  // the text of a key of a notice
#define THEME_NOTE_SCROLL      rgb(200, 208, 220)  // the scroll bar
#define THEME_NOTE_OK          rgb(120, 220, 150)  // the short note in the head (a done action)
#define THEME_NOTE_BTN         rgb(40, 48, 68)     // X and CLEAR ALL
#define THEME_NOTE_BTN_DOWN    rgb(80, 90, 120)    // X and CLEAR ALL under a finger
#define THEME_NOTE_BTN_EDGE    rgb(90, 100, 130)   // the edge of X and CLEAR ALL
#define THEME_NOTE_ARMED       rgb(120, 44, 30)    // CLEAR ALL after one tap (TAP AGAIN)
#define THEME_NOTE_ARMED_EDGE  rgb(255, 140, 100)  // the edge of TAP AGAIN
#define THEME_NOTE_BANNER_BG   rgb(26, 32, 46)     // the banner
#define THEME_NOTE_BANNER_EDGE rgb(76, 88, 116)    // the edge of the banner
#define THEME_NOTE_BANNER_TEXT rgb(240, 244, 250)  // the title on the banner
#define THEME_NOTE_BANNER_BODY rgb(196, 204, 220)  // the text on the banner
#define THEME_NOTE_BANNER_DIM  rgb(150, 160, 185)  // the age on the banner
#define THEME_NOTE_COUNT       rgb(232, 64, 56)    // the circle of the count of a burst
#define THEME_NOTE_COUNT_TEXT  rgb(255, 255, 255)  // the count of a burst
#define THEME_NOTE_GR_BG       rgb(0, 0, 0)        // the screen of the center (BOARD_UI_GRID: the black of the AMOLED)

// the keyboard (kbd.c) and its question (USE THIS TEXT?)
#define THEME_KBD_BG           rgb(12, 14, 28)     // the screen
#define THEME_KBD_TITLE        rgb(140, 150, 190)  // the title over the text
#define THEME_KBD_FIELD        rgb(30, 34, 60)     // the box of the text
#define THEME_KBD_FIELD_TEXT   rgb(255, 255, 255)  // the text
#define THEME_KBD_CURSOR       rgb(250, 215, 30)   // the cursor
#define THEME_KBD_KEY          rgb(36, 42, 72)     // a key
#define THEME_KBD_KEY_SPECIAL  rgb(50, 60, 100)    // SHIFT, DEL, SYM, SPACE
#define THEME_KBD_BTN          rgb(40, 48, 80)     // CANCEL, SHOW
#define THEME_KBD_ON           rgb(60, 170, 90)    // OK, SHOW that is on, SHIFT that is on
#define THEME_KBD_HOT          rgb(250, 215, 30)   // a key under a finger
#define THEME_KBD_FOCUS        rgb(250, 215, 30)   // the frame of the key with the focus
#define THEME_KBD_TEXT         rgb(255, 255, 255)  // the text of a key
#define THEME_KBD_TEXT_HOT     rgb(0, 0, 0)        // the text of a key under a finger
#define THEME_KBD_PREVIEW      rgb(250, 215, 30)   // the preview of the key under the finger
#define THEME_KBD_PREVIEW_EDGE rgb(255, 255, 255)  // the edge of the preview
#define THEME_KBD_PREVIEW_TEXT rgb(0, 0, 0)        // the letter in the preview
#define THEME_KBD_ASK_TEXT     rgb(255, 255, 255)  // the question
#define THEME_KBD_ASK_TEXT_DIM rgb(190, 200, 235)  // the line under the question
#define THEME_KBD_ASK_YES      rgb(40, 120, 70)    // YES
#define THEME_KBD_ASK_NO       rgb(60, 66, 100)    // NO
#define THEME_KBD_ASK_EDGE     rgb(200, 210, 240)  // the edge of YES and NO

// ------ the other system screens: STORE, FILES, FRIENDS, the calibration, the lobby of 2 boards, the update (app_store.c,
// app_files*.c, friends_page.c, calib_page.c, net2.c, ota.c)

// STORE (app_store.c): the field, the buttons, the progress, the toast, the row of icons, the page of an app
#define THEME_STORE_BG_TOP        rgb(34, 24, 66)     // the field at the top (a mix to the bottom colour)
#define THEME_STORE_BG_BOTTOM     rgb(10, 8, 24)      // the field at the bottom
#define THEME_STORE_BTN           rgb(52, 60, 120)    // a button
#define THEME_STORE_BTN_EDGE      rgb(130, 150, 255)  // the edge of a button
#define THEME_STORE_BTN_MAIN      rgb(46, 150, 92)    // the main button (INSTALL, OPEN)
#define THEME_STORE_BTN_MAIN_EDGE rgb(130, 240, 170)  // the edge of the main button
#define THEME_STORE_BTN_OFF       rgb(36, 34, 56)     // a button that does nothing now
#define THEME_STORE_BTN_OFF_EDGE  rgb(64, 60, 90)     // the edge of a button that does nothing now
#define THEME_STORE_BTN_TEXT      rgb(255, 255, 255)  // the text of a button and of the progress
#define THEME_STORE_BTN_TEXT_OFF  rgb(120, 116, 150)  // the text of a button that does nothing now
#define THEME_STORE_TRACK         rgb(30, 28, 56)     // the track of the install progress
#define THEME_STORE_TRACK_EDGE    rgb(90, 84, 150)    // the edge of the track
#define THEME_STORE_PROGRESS      rgb(46, 150, 92)    // the bar of the install progress
#define THEME_STORE_TOAST_BG      rgb(20, 70, 50)     // the short line after an install or a removal
#define THEME_STORE_TOAST_TEXT    rgb(200, 255, 220)  // its text
#define THEME_STORE_ACCENT        rgb(255, 216, 74)   // the name of the tab, the back mark, the dots of the load
#define THEME_STORE_DOT           rgb(90, 84, 130)    // the dot of a tab that is not shown
#define THEME_STORE_TEXT          rgb(255, 255, 255)  // the name of an app
#define THEME_STORE_TEXT_DIM      rgb(160, 152, 210)  // the facts of an app (the size, the version)
#define THEME_STORE_TEXT_FAINT    rgb(140, 132, 180)  // a hint, no description
#define THEME_STORE_SPACE         rgb(150, 145, 190)  // the free space at the top right
#define THEME_STORE_DESC          rgb(235, 230, 255)  // the description of an app
#define THEME_STORE_WARN          rgb(255, 196, 120)  // the permissions of an app (IT CAN ...)
#define THEME_STORE_STATE         rgb(235, 230, 255)  // the state of the store (LOADING, NOT REACHABLE)
#define THEME_STORE_STATE_DIM     rgb(170, 160, 210)  // the reason when the store is not reachable
#define THEME_STORE_EMPTY         rgb(210, 205, 240)  // the text of a tab with no apps
#define THEME_STORE_GLOW          rgb(110, 80, 200)   // the light behind the selected icon
#define THEME_STORE_SEL           rgb(60, 46, 110)    // the box of the selected icon
#define THEME_STORE_SEL_EDGE      rgb(200, 170, 255)  // its edge
#define THEME_STORE_SEL_NAME      rgb(220, 214, 250)  // the name below the selected icon (the 3.49)
#define THEME_STORE_PLACE_CARD    rgb(91, 140, 255)   // the dot of an app on the card
#define THEME_STORE_PLACE_BOARD   rgb(60, 210, 120)   // the dot of an app in the board storage
#define THEME_STORE_PANEL_BG      rgb(22, 18, 44)     // the panel of the details (the 3.49)
#define THEME_STORE_PANEL_EDGE    rgb(70, 60, 120)    // its edge
#define THEME_STORE_MARK          rgb(70, 60, 130)    // the mark of the app page ("i")
#define THEME_STORE_MARK_TEXT     rgb(235, 230, 255)  // its letter

// FILES (app_files*.c): the field, the rows, the buttons, the text, the colours of the kinds of entries
#define THEME_FILES_BG             rgb(7, 9, 16)       // the field
#define THEME_FILES_HEAD           rgb(15, 18, 30)     // the head bar
#define THEME_FILES_ROW            rgb(19, 23, 37)     // a row, a tile
#define THEME_FILES_HOT            rgb(48, 58, 96)     // a row or a tile under the finger
#define THEME_FILES_LINE           rgb(36, 42, 64)     // a line between the parts
#define THEME_FILES_BTN            rgb(33, 39, 62)     // a button
#define THEME_FILES_SEL            rgb(40, 52, 92)     // the folder of the list in the folder pane (the Tab5)
#define THEME_FILES_PANE           rgb(11, 14, 24)     // the folder pane and the preview pane (the Tab5)
#define THEME_FILES_TEXT           rgb(244, 246, 252)  // the text
#define THEME_FILES_TEXT_DIM       rgb(150, 158, 182)  // the dim text (the size, the time)
#define THEME_FILES_FAINT          rgb(90, 98, 124)    // a faint text, a locked entry
#define THEME_FILES_TEXT_ON_ACCENT rgb(0, 0, 0)        // the text on a button under the finger (on the accent)
#define THEME_FILES_ACCENT         rgb(251, 191, 36)   // the accent: a button under the finger, a focus edge, a folder
#define THEME_FILES_ACCENT_DIM     rgb(180, 128, 20)   // a folder in the folder pane (the Tab5)
#define THEME_FILES_BLUE           rgb(96, 165, 250)   // the card, a text file
#define THEME_FILES_GREEN          rgb(52, 211, 153)   // the board storage, a picture
#define THEME_FILES_VIOLET         rgb(167, 139, 250)  // an app package
#define THEME_FILES_FAULT          rgb(239, 68, 68)    // a full storage (the bar of the space)
#define THEME_FILES_FAULT_BG       rgb(150, 32, 32)    // a danger button, a fault message
#define THEME_FILES_OK_BG          rgb(22, 101, 52)    // a good button, a message of a good result
#define THEME_FILES_PIC_BG         rgb(0, 0, 0)        // the field of the picture page
#define THEME_FILES_PIC_TEXT       rgb(255, 255, 255)  // the text on the test picture (the PC only)

// FRIENDS (friends_page.c)
#define THEME_FRIENDS_BG         rgb(16, 14, 30)     // the field
#define THEME_FRIENDS_BACK       rgb(28, 24, 48)     // the BACK band at the left
#define THEME_FRIENDS_BACK_TEXT  rgb(255, 255, 255)  // its text
#define THEME_FRIENDS_TITLE      rgb(255, 255, 255)  // the title
#define THEME_FRIENDS_TEXT       rgb(255, 255, 255)  // the question of a removal
#define THEME_FRIENDS_TEXT_DIM   rgb(150, 140, 180)  // the hint (TAP A FRIEND TO REMOVE IT)
#define THEME_FRIENDS_HELP       rgb(200, 210, 240)  // the help of LEARN A FRIEND
#define THEME_FRIENDS_NAME       rgb(120, 230, 255)  // the name of this board
#define THEME_FRIENDS_WARN       rgb(255, 210, 120)  // LOOKING, NO FRIENDS YET
#define THEME_FRIENDS_OK         rgb(120, 255, 160)  // the message of a good result
#define THEME_FRIENDS_NEAR       rgb(120, 255, 160)  // the dot of a friend that is near
#define THEME_FRIENDS_BTN        rgb(40, 40, 80)     // a button
#define THEME_FRIENDS_BTN_EDGE   rgb(90, 230, 160)   // the edge of a button
#define THEME_FRIENDS_BTN_TEXT   rgb(255, 255, 255)  // the text of a button
#define THEME_FRIENDS_BTN_FRIEND rgb(30, 50, 70)     // the button of a friend
#define THEME_FRIENDS_BTN_ADD    rgb(70, 50, 20)     // LEARN A FRIEND, ADD
#define THEME_FRIENDS_BTN_ADDED  rgb(30, 70, 60)     // a board that is a friend now
#define THEME_FRIENDS_BTN_DANGER rgb(90, 30, 30)     // YES of a removal
#define THEME_FRIENDS_ASK_BG     rgb(20, 10, 30)     // the box of the question of a removal
#define THEME_FRIENDS_ASK_EDGE   rgb(255, 120, 120)  // its edge

// the calibration (calib_page.c). The target, the grid and the points of the touch test are a test pattern: they
// keep their numbers.
#define THEME_CALIB_BG             rgb(10, 10, 22)     // the field
#define THEME_CALIB_BACK           rgb(40, 44, 80)     // the back band at the left
#define THEME_CALIB_BACK_TEXT      rgb(255, 255, 255)  // its arrow
#define THEME_CALIB_TEXT           rgb(255, 255, 255)  // the title, a message, the name of a side
#define THEME_CALIB_TEXT_OFF       rgb(200, 200, 220)  // CALIBRATION: OFF
#define THEME_CALIB_OK             rgb(120, 240, 150)  // CALIBRATION: ON
#define THEME_CALIB_HELP           rgb(190, 196, 230)  // the help of the touch test
#define THEME_CALIB_TILT_HELP      rgb(200, 206, 240)  // the help of the tilt calibration
#define THEME_CALIB_BTN_EDGE       rgb(220, 230, 255)  // the edge of a button
#define THEME_CALIB_BTN_TEXT       rgb(255, 255, 255)  // the text of a button
#define THEME_CALIB_BTN_GO         rgb(30, 110, 70)    // CALIBRATE
#define THEME_CALIB_RESET          rgb(110, 40, 40)    // RESET
#define THEME_CALIB_RESET_ARMED    rgb(200, 60, 40)    // RESET after the first tap (TAP AGAIN)
#define THEME_CALIB_OK_BG          rgb(20, 80, 40)     // the band of a good result
#define THEME_CALIB_FAULT_BG       rgb(90, 30, 30)     // the band of a bad result
#define THEME_CALIB_SIDE           rgb(30, 34, 60)     // a side of the board that has no reading
#define THEME_CALIB_SIDE_EDGE      rgb(90, 96, 140)    // its edge
#define THEME_CALIB_SIDE_DONE      rgb(20, 90, 50)     // a side that has a reading
#define THEME_CALIB_SIDE_DONE_EDGE rgb(120, 240, 150)  // its edge
#define THEME_CALIB_SIDE_VALUE     rgb(220, 230, 255)  // the reading of a side
#define THEME_CALIB_NOW            rgb(255, 230, 120)  // the line of the reading now
#define THEME_CALIB_TRACK          rgb(40, 44, 70)     // the track of the still bar
#define THEME_CALIB_BAR            rgb(255, 230, 120)  // the still bar
#define THEME_CALIB_BAR_DONE       rgb(120, 240, 150)  // the still bar when the side is taken

// the lobby of 2 boards (net2.c)
#define THEME_LOBBY_BG             rgb(10, 8, 24)      // the box
#define THEME_LOBBY_EDGE           rgb(90, 230, 160)   // its edge
#define THEME_LOBBY_TEXT           rgb(255, 255, 255)  // the text
#define THEME_LOBBY_TEXT_DIM       rgb(200, 210, 240)  // the second line
#define THEME_LOBBY_WARN           rgb(255, 210, 120)  // the last line (TAP TO STOP, a friend added)
#define THEME_LOBBY_BTN            rgb(30, 70, 60)     // the button of a friend
#define THEME_LOBBY_BTN_EDGE       rgb(90, 230, 160)   // its edge
#define THEME_LOBBY_BTN_TEXT       rgb(255, 255, 255)  // its text
#define THEME_LOBBY_BADGE          rgb(80, 50, 10)     // the ADD badge of a new board
#define THEME_LOBBY_BADGE_EDGE     rgb(255, 200, 90)   // its edge
#define THEME_LOBBY_BADGE_NEW      rgb(255, 120, 60)   // its NEW mark
#define THEME_LOBBY_BADGE_NEW_TEXT rgb(255, 255, 255)  // the text of the NEW mark
#define THEME_LOBBY_BADGE_TEXT     rgb(255, 240, 200)  // the text of the badge
#define THEME_LOBBY_LOST_BG        rgb(60, 10, 20)     // the band THE OTHER BOARD IS GONE
#define THEME_LOBBY_LOST_TEXT      rgb(255, 170, 170)  // its text

// the update (ota.c): the box over the screen
#define THEME_OTA_BG       rgb(14, 18, 40)     // the box
#define THEME_OTA_EDGE     rgb(120, 200, 255)  // its edge
#define THEME_OTA_FAULT    rgb(240, 80, 80)    // its edge after a fault
#define THEME_OTA_TEXT     rgb(255, 255, 255)  // the title
#define THEME_OTA_TEXT_DIM rgb(210, 220, 255)  // the line of the state
#define THEME_OTA_TRACK    rgb(40, 46, 80)     // the track of the progress
#define THEME_OTA_BAR      rgb(90, 220, 140)   // the bar of the progress
