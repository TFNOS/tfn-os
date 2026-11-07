// Made by main/apps_gen.py from main/apps.json: do not edit (change main/apps.json).
// ALL apps of main/apps.json, in the menu order, also the apps that are off: app.h takes this list when
// there is no apps_gen.h (a host test with only -I main). The firmware and tests/host/build.sh use the
// list of their build folder (apps_list.inc). An app with a guard is in only when its switch is 1.
// ARCADE_APP(ID, sym, group, is_new, x349, y349, xs3, ys3, test_ms): ID = the C name (ARCADE_APP_<ID>),
// sym = the app_t, group = GAMES / UTIL / SETTINGS / SYSTEM (not in the menu), is_new = the NEW pill,
// x349 y349 / xs3 ys3 = the self-test tap point (logical) on the 3.49 / the CoreS3 (-1 = no self-test),
// test_ms = the self-test time of the app.
ARCADE_APP(MENU, app_menu, SYSTEM, 0, -1, -1, -1, -1, 0)
ARCADE_APP(DICE, app_dice, UTIL, 1, 300, 86, 193, 140, 6000)
ARCADE_APP(SIMON, app_simon, GAMES, 1, 320, 86, 160, 120, 6000)
ARCADE_APP(CLOCK, app_clock, UTIL, 1, 320, 90, 160, 120, 6000)
ARCADE_APP(I2C, app_i2c, UTIL, 1, 320, 90, 160, 120, 6000)
ARCADE_APP(SERIAL, app_serial, UTIL, 1, 320, 90, 160, 120, 6000)
ARCADE_APP(MQTT, app_mqtt, UTIL, 1, 320, 90, 160, 120, 6000)
ARCADE_APP(WEATHER, app_weather, UTIL, 1, 86, 320, 160, 120, 6000)
ARCADE_APP(STACK, app_stack, GAMES, 0, 86, 300, 160, 120, 6000)
ARCADE_APP(STORE, app_store, UTIL, 1, 86, 300, 160, 120, 6000)
ARCADE_APP(FILES, app_files, UTIL, 1, 480, 110, 160, 190, 6000)
ARCADE_APP(TOUCHLAB, app_touchlab, UTIL, 1, 320, 86, 160, 120, 6000)
ARCADE_APP(TALKIE, app_talkie, UTIL, 1, 320, 6, 160, 6, 6000)
ARCADE_APP(LEVEL, app_level, UTIL, 0, 320, 150, 270, 62, 6000)
ARCADE_APP(GAMEPAD, app_gamepad, UTIL, 0, 320, 150, 160, 150, 6000)
ARCADE_APP(MIDICTL, app_midictl, UTIL, 0, 520, 20, 160, 232, 6000)
ARCADE_APP(WALKIE, app_walkie, UTIL, 0, 320, 20, 160, 20, 6000)
ARCADE_APP(SETTINGS, app_settings, SETTINGS, 0, 566, 70, 316, 120, 6000)
