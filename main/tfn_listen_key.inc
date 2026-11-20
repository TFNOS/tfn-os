// TFN OS: THE TALK KEY (a part of tfn_listen.c). Hold the BOOT key in any app, talk, let go. The recording starts
// when the key goes down (no word start is lost); a short press stays a short press (the recording is dropped). After
// KEY_LONG_MS the sign "LISTENING" shows. A key that goes down while the ears or a brain work (a turn of the app) waits
// with the sign "WAIT": the recording starts when they end. At the release the main brain of TALK understands the
// sentence: a command of the board runs here, else the handler of the open app gets it, else the answer shows over the
// app and the voice says it. The setting "talk_key" (store, 1 = on: the default when the ears are ready; 0 = the long
// press opens CHAT).

#define NOTE_N       200
#define NOTE_MIN_MS  2500
#define TALK_APP     "TALK"
#define TALK_VOL_MAX AUDIO_VOL_MAX

static const tune_note_t K_TONE[] = {{0, 70, 76, 0, W_SINE, 70}, {70, 110, 83, 0, W_SINE, 70}};
static char s_say[NOTE_N];
static bool s_key_on;  // (read at key down: store_get is safe on the main task)

static void note_show(const char *t)
{
    snprintf(s_say, sizeof s_say, "%s", t);
    for (char *p = s_say; *p; p++) {
        *p = (char)toupper((unsigned char)*p);  // (the system faces have capitals only)
    }
    s_note_until = tfn_now_ms() + NOTE_MIN_MS + 60 * (int64_t)strlen(t);
}

static bool talk_open_now(void)
{
    const app_t *a = app_current();
    return a && !strcasecmp(a->name, TALK_APP);  // (TALK has its own ears: no second copy of the models)
}

bool listen_key_down(void)
{
    s_key_on = listen_ready() && store_get("talk_key", 1) && !talk_open_now();
    if (!s_key_on) {
        return false;
    }
    if (s_key != 3 && start(NULL)) {
        s_key = 1;
        return true;
    }
    if (s_key == 3 || busy()) {
        // the ears or a brain work now (a turn of the app, the last turn of the key): the key waits, and the long
        // press of CHAT does not run (else a hold in an app that thinks opened CHAT)
        s_wait = 1;
        return true;
    }
    s_key_on = false;  // (no memory for the recording)
    return false;
}

void listen_key_held(int ms)
{
    if (s_wait == 1 && ms >= KEY_LONG_MS) {
        s_wait = 2;  // (the sign WAIT)
    } else if (s_key == 1 && ms >= KEY_LONG_MS) {
        s_key = 2;
        speech_stop();  // (the voice of an earlier answer)
    }
}

void listen_key_up(void)
{
    if (s_wait) {
        s_wait = 0;  // (it went up before the ears were free: no talk)
    } else if (s_key == 1) {
        s_key = 0;
        listen_cancel();  // a short press: no talk
    } else if (s_key == 2) {
        s_key = 3;
        listen_stop();
    }
}

static int vol_now(void)
{
    return g_settings.mute ? 0 : g_settings.volume;
}

static void set_volume(int v, bool mute)
{
    g_settings.volume = (uint8_t)(v < 0 ? 0 : v > TALK_VOL_MAX ? TALK_VOL_MAX : v);
    g_settings.mute = mute;
    store_set("volume", g_settings.volume);
    store_set("mute", mute);
    audio_alert(K_TONE, 2, 10);
}

static int bright_pc(void)
{
    return ((g_settings.brightness - 10) * 100 + 122) / 245;
}

static void set_bright(int pc)
{
    pc = pc < 0 ? 0 : pc > 100 ? 100 : pc;
    g_settings.brightness = (uint8_t)(10 + pc * 245 / 100);
    board_backlight(g_settings.brightness);
    store_set("bright", g_settings.brightness);
}

static bool local_now(tz_tm_t *t)
{
    if (!clock_time_ok()) {
        return false;
    }
    char z[64];
    tz_rule_t r;
    alarm_tz_get(z, sizeof z);
    const int64_t utc = (int64_t)time(NULL);
    tz_split(utc + (tz_parse(z, &r) ? tz_offset(&r, utc, NULL) : 0), t);
    return true;
}

// ---- THE TIMERS AND THE ALARMS by voice (tfn/alarm.h: they ring in any app, also with the screen off)
// a length as words: "5 minutes", "1 hour 30 minutes", "45 seconds"
static void dur_text(int s, char *out, int cap)
{
    const int h = s / 3600, m = s / 60 % 60, sec = s % 60;
    int n = 0;
    out[0] = 0;
    if (h) {
        n += snprintf(out + n, (size_t)(cap - n), "%d hour%s", h, h == 1 ? "" : "s");
    }
    if (m && n < cap) {
        n += snprintf(out + n, (size_t)(cap - n), "%s%d minute%s", n ? " " : "", m, m == 1 ? "" : "s");
    }
    if ((sec || !n) && n < cap) {
        snprintf(out + n, (size_t)(cap - n), "%s%d second%s", n ? " " : "", sec, sec == 1 ? "" : "s");
    }
}

// a new timer of sec seconds (as a timer of CLOCK: CHIME, volume 70); false: no free place (4 timers)
static bool voice_timer(int sec)
{
    ctimer_t t[TIMER_MAX];
    timer_get(t, TIMER_MAX);
    for (int i = 0; i < TIMER_MAX; i++) {
        if (t[i].state == TIMER_FREE || t[i].state == TIMER_DONE) {
            memset(&t[i], 0, sizeof t[i]);
            t[i].state = TIMER_RUN;
            t[i].len_s = (uint32_t)sec;
            t[i].end_ms = esp_timer_get_time() / 1000 + (int64_t)sec * 1000;
            t[i].sound = 1;
            t[i].vol = 70;
            snprintf(t[i].label, sizeof t[i].label, "TALK");
            return timer_set(t, TIMER_MAX);
        }
    }
    return false;
}

// the running timer that ends first: the ms that are left; -1 = none
static int64_t voice_timer_left(void)
{
    ctimer_t t[TIMER_MAX];
    timer_get(t, TIMER_MAX);
    const int64_t now = esp_timer_get_time() / 1000;
    int64_t best = -1;
    for (int i = 0; i < TIMER_MAX; i++) {
        const int64_t left = t[i].state == TIMER_RUN     ? (t[i].end_ms > now ? t[i].end_ms - now : 0)
                             : t[i].state == TIMER_PAUSE ? t[i].left_ms
                                                         : -1;
        if (left >= 0 && (best < 0 || left < best)) {
            best = left;
        }
    }
    return best;
}

// cancel the timers of TALK (else all the timers that run): the count
static int voice_timer_off(void)
{
    ctimer_t t[TIMER_MAX];
    timer_get(t, TIMER_MAX);
    int n = 0;
    for (int pass = 0; pass < 2 && !n; pass++) {
        for (int i = 0; i < TIMER_MAX; i++) {
            const bool live = t[i].state == TIMER_RUN || t[i].state == TIMER_PAUSE;
            if (live && (pass || !strcmp(t[i].label, "TALK"))) {
                t[i].state = TIMER_FREE;
                n++;
            }
        }
    }
    if (n) {
        timer_set(t, TIMER_MAX);
    }
    return n;
}

// a once alarm at h:m (as a new alarm of CLOCK: volume 70, snooze 9 min); the same time again turns it on
static bool voice_alarm(int h, int m)
{
    alarm_t a[ALARM_MAX];
    const int n = alarm_get(a, ALARM_MAX);
    for (int i = 0; i < n; i++) {
        if (a[i].hour == h && a[i].min == m && a[i].days == 0) {
            a[i].on = 1;
            return alarm_set(a, n);
        }
    }
    if (n >= ALARM_MAX) {
        return false;
    }
    memset(&a[n], 0, sizeof a[n]);
    a[n].on = 1;
    a[n].hour = (uint8_t)h;
    a[n].min = (uint8_t)m;
    a[n].vol = 70;
    a[n].snooze = 9;
    return alarm_set(a, n + 1);
}

// a code of the main brain; out = the note (an action) or the answer (a question: the voice says it). true: a code.
static bool run_code(const char *c, char *out, int cap, bool *say)
{
    static const char *const DAY[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const MON[] = {"January", "February", "March",     "April",   "May",      "June",
                                      "July",    "August",   "September", "October", "November", "December"};
    *say = false;
    const int vol = vol_now();
    if (!strcmp(c, "@vol+") || !strcmp(c, "@vol-") || !strncmp(c, "@vol=", 5)) {
        int v = c[4] == '=' ? atoi(c + 5) : vol + (c[4] == '+' ? 15 : -15);
        if (c[4] == '+') {
            v = vol > 100 ? vol : v > 100 ? 100 : v;  // (the boost over 100 only from the panel or a number)
        }
        set_volume(v, false);
        snprintf(out, (size_t)cap, "Volume %d%%", g_settings.volume);
    } else if (!strcmp(c, "@mute")) {
        set_volume(g_settings.volume, true);
        snprintf(out, (size_t)cap, "Sound off");
    } else if (!strcmp(c, "@unmute")) {
        set_volume(g_settings.volume < 10 ? 40 : g_settings.volume, false);
        snprintf(out, (size_t)cap, "Sound on, volume %d%%", g_settings.volume);
    } else if (!strcmp(c, "@bri+") || !strcmp(c, "@bri-") || !strncmp(c, "@bri=", 5)) {
        set_bright(c[4] == '=' ? atoi(c + 5) : bright_pc() + (c[4] == '+' ? 20 : -20));
        snprintf(out, (size_t)cap, "Brightness %d%%", bright_pc());
    } else if (!strncmp(c, "@open ", 6)) {
        const app_t *a = app_find(c + 6);
        if (!a) {
            return false;
        }
        snprintf(out, (size_t)cap, "Opening %s", a->name);
        app_open(a);
    } else if (!strcmp(c, "@home")) {
        snprintf(out, (size_t)cap, "Menu");
        app_open(&app_menu);
    } else if (!strcmp(c, "@panel")) {
        snprintf(out, (size_t)cap, "Quick settings");
        sysui_panel_open();
    } else if (!strncmp(c, "@timer=", 7)) {
        const int sec = atoi(c + 7);
        char d[48];
        dur_text(sec, d, sizeof d);
        if (sec <= 0) {
            return false;
        }
        *say = true;  // (the voice says the length: the person hears what the board understood)
        snprintf(out, (size_t)cap, voice_timer(sec) ? "A timer of %s." : "All four timers are in use.", d);
    } else if (!strcmp(c, "@timer0")) {
        if (alarm_ringing() >= ALARM_MAX) {
            alarm_stop();  // (a timer rings: it stops)
            snprintf(out, (size_t)cap, "Timer stopped");
        } else {
            const int n = voice_timer_off();
            snprintf(out, (size_t)cap, n ? "Timer cancelled" : "No timer runs");
        }
    } else if (!strncmp(c, "@alarm=", 7)) {
        const int mins = atoi(c + 7);
        const char half = c[strlen(c) - 1];
        int h = mins / 60, m = mins % 60;
        tz_tm_t t;
        if (half == 'p' && h < 12) {
            h += 12;
        } else if (half == 'a' && h == 12) {
            h = 0;
        } else if (half != 'a' && half != 'p' && h >= 1 && h <= 12 && local_now(&t)) {
            const int now = t.h * 60 + t.mi, am = (h % 12) * 60 + m, pm = am + 720;
            h = (am > now ? am : pm > now ? pm : am) / 60;  // (no AM or PM said: the next of the two)
        }
        if (h > 23 || m > 59) {
            return false;
        }
        *say = true;
        snprintf(out, (size_t)cap, voice_alarm(h, m) ? "An alarm at %d:%02d %s." : "All eight alarms are in use.",
                 (h + 11) % 12 + 1, m, h < 12 ? "AM" : "PM");
    } else if (!strcmp(c, "@stop") || !strcmp(c, "@snooze")) {
        if (alarm_ringing() < 0) {
            return false;  // (nothing rings: the text goes to the open app)
        }
        if (c[1] == 's' && c[2] == 'n') {
            alarm_snooze();
            snprintf(out, (size_t)cap, "Snooze");
        } else {
            alarm_stop();
            snprintf(out, (size_t)cap, "Stopped");
        }
    } else {
        *say = true;
        tz_tm_t t;
        if (!strcmp(c, "@vol?")) {
            snprintf(out, (size_t)cap, g_settings.mute ? "The sound is off." : "The volume is %d percent.", vol);
        } else if (!strcmp(c, "@bri?")) {
            snprintf(out, (size_t)cap, "The brightness is %d percent.", bright_pc());
        } else if (!strcmp(c, "@time") || !strcmp(c, "@date")) {
            if (!local_now(&t)) {
                snprintf(out, (size_t)cap, "I do not know the time yet.");
            } else if (c[1] == 't') {
                snprintf(out, (size_t)cap, "It is %d:%02d %s.", (t.h + 11) % 12 + 1, t.mi, t.h < 12 ? "AM" : "PM");
            } else {
                snprintf(out, (size_t)cap, "Today is %s, %d %s.", DAY[t.wd % 7], t.d, MON[(t.mo + 11) % 12]);
            }
        } else if (!strcmp(c, "@wifi")) {
            snprintf(out, (size_t)cap,
                     wifi_state() == WIFI_CONNECTED ? "I am connected to WiFi." : "I am not connected to WiFi now.");
        } else if (!strcmp(c, "@timer?")) {
            const int64_t left = voice_timer_left();
            char d[48];
            dur_text((int)((left + 999) / 1000), d, sizeof d);
            snprintf(out, (size_t)cap, left < 0 ? "No timer runs." : "%s left.", d);
        } else if (!strcmp(c, "@help")) {
            snprintf(out, (size_t)cap,
                     "Hold the key and say: volume up, brightness fifty, open chess, what time is it.");
        } else {
            return false;
        }
    }
    return true;
}

// the end of a talk-key turn (the main task)
static void key_turn_end(void)
{
    char out[NOTE_N] = "";
    bool say = true;
    if (s_state == LS_FAULT) {
        snprintf(out, sizeof out, "I could not hear that.");
        say = false;
    } else if (!s_text[0]) {
        snprintf(out, sizeof out, "I heard nothing.");
        say = false;
    } else if (s_ans[0] == '@' && run_code(s_ans, out, sizeof out, &say)) {
        ESP_LOGI(TAG, "the talk key: [%s] -> %s", s_text, s_ans);
    } else if (s_handler && s_handler(s_text, s_ans)) {
        out[0] = 0;  // (the app used it)
    } else {
        snprintf(out, sizeof out, "%.190s", s_ans[0] && s_ans[0] != '@' ? s_ans : "Sorry, I did not get that.");
    }
    if (out[0]) {
        note_show(out);
        if (say && speech_ready()) {
            speech_say(out);
        }
    }
    s_state = LS_OFF;
}

// a waiting talk key: the recording starts when the ears and the brains are free, and the app read the result of
// its turn (else the new recording deletes it). An app that does not read it: 1 s
static void key_wait_step(int64_t now)
{
    static int64_t done_t;
    if (s_key == 3 || busy()) {
        done_t = 0;
        return;
    }
    if (s_state == LS_DONE && !s_taken) {
        if (!done_t) {
            done_t = now;
        }
        if (now - done_t < 1000) {
            return;
        }
    }
    done_t = 0;
    if (start(NULL)) {
        s_key = s_wait == 2 ? 2 : 1;
        if (s_key == 2) {
            speech_stop();  // (the voice of the answer that came)
        }
    }
    s_wait = 0;  // (a fault at the start: no talk)
}

void listen_tick(int64_t now)
{
    if (s_key == 3 && (s_state == LS_DONE || s_state == LS_FAULT)) {
        s_key = 0;
        key_turn_end();
    } else if (s_wait) {
        key_wait_step(now);
    } else if (s_key && s_state == LS_OFF) {
        s_key = 0;  // (dropped: a cancel, a fault at the start)
    }
}

// the sign: it slides down from the top (an ease), a soft shadow, a glow that pulses, live level bars
static int64_t s_sign_t0;  // when the sign came (0: no sign)
static float s_lv[5];

static float ease_out(float x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}

static int sign_y(int h, int u)
{
    const float k = ease_out((float)(tfn_now_ms() - s_sign_t0) / 260.0f);
    const int y1 = BOARD_ROUND ? gfx_h / 7 : 5 * u;
    return (int)(-h + (y1 + h) * k);
}

static void pill(const char *t, uint16_t bg, uint16_t glow, float level, bool wave)
{
    const mfont_t *f = gfx_native() ? &MF_N20 : &MF_LABEL;
    const int u = gfx_native() ? 2 : 1;
    const int h = f->line + 12 * u, bars = 5 * 5 * u;
    const int w = mf_width(f, t, 1) + bars + 34 * u;
    const int x = (gfx_w - w) / 2, y = sign_y(h, u);
    const int64_t now = tfn_now_ms();
    const float pulse = 0.5f + 0.5f * fsin((float)(now % 1200) / 1200.0f * 6.2832f);
    ui_rrect(x + 2 * u, y + 3 * u, w, h, h / 2, rgb(0, 0, 0));  // (the shadow)
    ui_rrect(x - 2 * u, y - 2 * u, w + 4 * u, h + 4 * u, h / 2 + 2 * u, ui_mix(bg, glow, (int)(pulse * 255)));
    ui_rrect(x, y, w, h, h / 2, bg);
    mf_text(f, x + 16 * u, y + (h + f->asc) / 2, t, rgb(255, 255, 255), 255, 1);
    const int bx = x + w - bars - 12 * u, cy = y + h / 2;
    for (int i = 0; i < 5; i++) {
        // the voice: each bar follows the level with its own speed; the brain works: a soft wave
        const float want = wave ? 0.25f + 0.25f * fsin((float)now / 160.0f - (float)i * 0.9f)
                                : level * (0.6f + 0.4f * fsin((float)now / 90.0f + (float)i * 1.7f));
        s_lv[i] += (want - s_lv[i]) * 0.35f;
        const int bh = 3 * u + (int)((h - 12 * u) * (s_lv[i] < 0 ? 0 : s_lv[i] > 1 ? 1 : s_lv[i]));
        ui_rrect(bx + i * 5 * u, cy - bh / 2, 3 * u, bh, u + 1, rgb(255, 255, 255));
    }
}

// the answer: up to 3 lines in a dark card that slides in, a coloured edge on the left
static void note_box(void)
{
    const mfont_t *f = gfx_native() ? &MF_N16 : &MF_TINY;
    const int u = gfx_native() ? 2 : 1;
    const int maxw = (BOARD_ROUND ? gfx_w * 2 / 3 : gfx_w - 24 * u) - 20 * u;
    char lines[3][NOTE_N];
    int n = 0;
    const char *p = s_say;
    while (*p && n < 3) {
        int k = 0, cut = 0;
        char tmp[NOTE_N];
        while (p[k]) {
            snprintf(tmp, sizeof tmp, "%.*s", k + 1, p);
            if (mf_width(f, tmp, 1) > maxw) {
                break;
            }
            k++;
            if (p[k] == ' ' || !p[k]) {
                cut = k;
            }
        }
        if (!cut) {
            cut = k ? k : 1;
        }
        snprintf(lines[n++], NOTE_N, "%.*s", cut, p);
        p += cut;
        while (*p == ' ') {
            p++;
        }
    }
    const int h = n * f->line + 14 * u, w = maxw + 20 * u;
    const int x = (gfx_w - w) / 2, y = sign_y(h, u);
    ui_rrect(x + 2 * u, y + 3 * u, w, h, 9 * u, rgb(0, 0, 0));
    ui_rrect(x, y, w, h, 9 * u, rgb(70, 110, 255));
    ui_rrect(x + 3 * u, y, w - 3 * u, h, 9 * u, rgb(22, 26, 42));
    for (int i = 0; i < n; i++) {
        mf_text_c(f, x + w / 2 + u, y + 7 * u + i * f->line + f->asc, lines[i], rgb(235, 240, 255), 255, 1);
    }
}

bool listen_shows(void)
{
    return (s_key == 2 && s_state == LS_REC) || s_key == 3 || s_wait == 2 || (s_say[0] && tfn_now_ms() < s_note_until);
}

void listen_overlay(void)
{
    static int shown;  // 0 none, 1 listen, 2 work, 3 the answer, 4 the key waits
    int want = 0;
    if (s_key == 2 && s_state == LS_REC) {
        want = 1;
    } else if (s_wait == 2) {
        want = 4;
    } else if (s_key == 3) {
        want = 2;
    } else if (s_say[0] && tfn_now_ms() < s_note_until) {
        want = 3;
    } else {
        s_say[0] = 0;
    }
    if (want != shown) {
        s_sign_t0 = want && !shown ? tfn_now_ms() : want ? s_sign_t0 : 0;
        shown = want;
    }
    if (want) {
        gfx_clip_reset();  // (the sign draws over all of the frame: not in a clip that the app left)
        gfx_clip_box_off();
    }
    if (want == 1) {
        pill("LISTENING", rgb(205, 40, 70), rgb(255, 120, 150), audio_mic_level() * 4.0f, false);
    } else if (want == 2) {
        pill(s_state == LS_THINK ? "THINKING" : "HEARING", rgb(70, 60, 190), rgb(150, 140, 255), 0.0f, true);
    } else if (want == 3) {
        note_box();
    } else if (want == 4) {
        pill("WAIT", rgb(70, 66, 80), rgb(160, 150, 180), 0.0f, true);
    }
}

void listen_console(const char *a, const char *b)
{
    static const char *const ST[] = {"OFF", "REC", "HEAR", "THINK", "DONE", "FAULT"};
    if (!a || !a[0]) {
        printf("listen: %s, state %s, key %d, open %d, talk_key %u\n", listen_ready() ? "ready" : "no ears",
               ST[s_state], s_key, s_open, (unsigned)store_get("talk_key", 1));
        printf("  heard [%s] answer [%s]\n", s_text, s_ans);
    } else if (!strcmp(a, "ask") && b) {
        printf(listen_ask(NULL, b) ? "listen: asked\n" : "listen: busy or no ears\n");
    } else if (!strcmp(a, "key") && b) {
        s_key = 3;  // (a talk-key turn of a typed text: the command runs as from the key)
        printf(listen_ask(NULL, b) ? "listen: key turn\n" : "listen: busy or no ears\n");
    } else if (!strcmp(a, "down")) {
        // a virtual talk key (a test with a voice from a speaker): down = it records and shows LISTENING
        const bool ok = listen_key_down();
        if (ok) {
            listen_key_held(KEY_LONG_MS);  // (as a real long press: LISTENING, the voice stops)
        }
        ESP_LOGI(TAG, "%s",
                 ok ? (s_wait ? "the key waits for the ears" : "the key is down")
                    : "no talk key (no ears, the setting, TALK open)");
    } else if (!strcmp(a, "up")) {
        listen_key_up();
        ESP_LOGI(TAG, "the key is up");
    } else if (!strcmp(a, "close")) {
        tfa_listen_quiet();
        printf("listen: the module closes when idle\n");
    } else if (!strcmp(a, "file") && b) {
        snprintf(s_file, sizeof s_file, "%s", b);  // (a test with no sound in the room)
        printf("listen: the next turn uses %s\n", s_file);
    } else {
        printf("listen [ask <text> | key <text> | down | up | file <raw path> | close]\n");
    }
}
