// The unit test of the rule of a consent (main/input.c input_injected(), the consent helper of main/input.h): a tap of
// a consent (ALLOW, LEAVE, a new permission) acts only for a finger on the panel, of a touch that came down
// INPUT_ARM_MS or more after the question showed. The inputs that are not a finger: input_inject() (the console "tap",
// a key of the board) and the virtual fingers (the console "finger", "fmove", "finger2", a tap of the manager page:
// /api/ctl). The touch task marks each event at its source, so the answer does not change when the app task reads the
// events late (a slow frame, an app that opens), and the reports that wait in the hold of the pull zone keep their
// mark. A tap answers only for the touch whose IN_DOWN the question got (not after a long press or a drag, not at the
// wake from the screen saver). The test runs the real touch task of input.c: the fake FreeRTOS of
// tests/unit/fake_input/ gives the control back to the test after a given number of reports (10 ms each), and the test
// reads the real events.
// Build and run: python tests/unit/run.py
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "input.h"
#include "settings.h"

// ---------------------------------------------------------------- the fake clock, the panel and the store
static int64_t s_now_us = 1000LL * 1000 * 1000;  // (the test starts at 1000 s: no input before it)
static int s_fx = -1, s_fy = -1;                 // the finger on the panel (-1 = none)

int64_t esp_timer_get_time(void)
{
    return s_now_us;
}

int board_touch_points(int *x, int *y, int max)
{
    if (s_fx < 0 || max < 1) {
        return 0;
    }
    x[0] = s_fx;
    y[0] = s_fy;
    return 1;
}

int board_touch_int_gpio(void)
{
    return -1;
}

int board_wheel_pos(void)
{
    return 0;
}

void board_touch_int_ack(void)
{
}

int power_x(void)
{
    return 0;  // (no power test switches: the normal touch read)
}

uint32_t store_get(const char *key, uint32_t def)
{
    (void)key;
    return def;
}

void store_set(const char *key, uint32_t val)
{
    (void)key;
    (void)val;
}

esp_err_t esp_sleep_enable_gpio_wakeup(void)
{
    return ESP_OK;
}

// (the touch INT line: the test board has none, so input.c calls no GPIO function)
esp_err_t gpio_config(const gpio_config_t *c)
{
    (void)c;
    return ESP_FAIL;
}

esp_err_t gpio_install_isr_service(int flags)
{
    (void)flags;
    return ESP_FAIL;
}

esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t fn, void *arg)
{
    (void)pin;
    (void)fn;
    (void)arg;
    return ESP_FAIL;
}

esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t type)
{
    (void)pin;
    (void)type;
    return ESP_OK;
}

esp_err_t gpio_intr_enable(gpio_num_t pin)
{
    (void)pin;
    return ESP_OK;
}

esp_err_t gpio_intr_disable(gpio_num_t pin)
{
    (void)pin;
    return ESP_OK;
}

esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type)
{
    (void)pin;
    (void)type;
    return ESP_OK;
}

// ---------------------------------------------------------------- the fake FreeRTOS
struct fk_queue {
    size_t item;
    int len, r, n;
    unsigned char *buf;
};

QueueHandle_t xQueueCreate(int len, size_t item)
{
    QueueHandle_t q = calloc(1, sizeof *q);
    if (q) {
        q->item = item;
        q->len = len;
        q->buf = calloc((size_t)len, item);
    }
    return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)wait;
    if (!q || !q->buf || q->n == q->len) {
        return pdFALSE;
    }
    memcpy(q->buf + (size_t)((q->r + q->n) % q->len) * q->item, item, q->item);
    q->n++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)wait;
    if (!q || !q->n) {
        return pdFALSE;
    }
    memcpy(item, q->buf + (size_t)q->r * q->item, q->item);
    q->r = (q->r + 1) % q->len;
    q->n--;
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    if (q) {
        q->n = 0;
    }
    return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return NULL;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
    (void)s;
    (void)woken;
    return pdFALSE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    (void)s;
    (void)wait;
    return pdFALSE;
}

// the touch task of input.c: the test runs it for s_left reports, then its delay jumps back to the test
static void (*s_task)(void *);
static jmp_buf s_back;
static int s_left;

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                                   TaskHandle_t *out, int core)
{
    (void)name;
    (void)stack;
    (void)arg;
    (void)prio;
    (void)out;
    (void)core;
    s_task = fn;
    return pdTRUE;
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(s_now_us / 1000);
}

void vTaskDelayUntil(TickType_t *wake, TickType_t ticks)
{
    if (s_left-- <= 0) {
        longjmp(s_back, 1);
    }
    *wake += ticks;
    s_now_us += (int64_t)ticks * 1000;  // (the clock goes on to the next report)
}

void vTaskDelay(TickType_t ticks)
{
    s_now_us += (int64_t)ticks * 1000;
}

// ---------------------------------------------------------------- the steps of the test
static int s_fails;

static void expect(bool ok, const char *what)
{
    if (!ok) {
        s_fails++;
        printf("FAIL %s\n", what);
    }
}

static int64_t now_ms(void)
{
    return s_now_us / 1000;
}

// the touch task makes n reports (10 ms each)
static void reports(int n)
{
    s_left = n;
    if (!setjmp(s_back)) {
        s_task(NULL);
    }
}

static void wait_ms(int ms)
{
    reports(ms / 10);
}

static void finger_down(int x, int y)
{
    s_fx = x;
    s_fy = y;
}

static void finger_up(void)
{
    s_fx = -1;
    s_fy = -1;
}

// a finger on the panel at (x, y) for ms, then up (the IN_UP and the IN_TAP come 40 ms after the last contact)
static void panel_tap(int x, int y, int ms)
{
    finger_down(x, y);
    reports(ms / 10);
    finger_up();
    reports(8);
}

// The app task reads the events now (late = the time that they waited in the queue). The answers to the taps, as the
// rule gives them to sysui.c and the pages of SETUP: 1 = by a finger, 0 = not by a finger. out: up to 4 taps; returns
// the count.
static int read_taps(int *out)
{
    input_event_t ev;
    int n = 0;
    while (input_get(&ev)) {
        if (ev.kind == IN_TAP && n < 4) {
            out[n++] = input_injected(&ev) ? 0 : 1;
        }
    }
    return n;
}

// one tap in the queue: its answer (-1 = no tap, or more than one)
static int one_tap(void)
{
    int t[4];
    return read_taps(t) == 1 ? t[0] : -1;
}

// the console "tap" (console_input.c con_tap): DOWN, UP, TAP into the queue
static void console_tap(void)
{
    input_event_t d = {.kind = IN_DOWN, .x = 80, .y = 300, .t_ms = now_ms()};
    input_event_t u = d, tp = d;
    u.kind = IN_UP;
    tp.kind = IN_TAP;
    u.t_ms = tp.t_ms = d.t_ms + 50;
    input_inject(&d);
    input_inject(&u);
    input_inject(&tp);
}

// a tap of the manager page (web_walkie.c page_finger: /api/ctl?tap): a virtual finger for 90 ms
static void web_tap(int x, int y)
{
    input_virtual_finger(x, y, now_ms() + 90);
    wait_ms(200);
}

static void test_console(void)
{
    wait_ms(1000);
    console_tap();
    expect(one_tap() == 0, "the console tap (input_inject) is refused");
    wait_ms(100);
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "a finger on the panel 100 ms after a console tap is taken (the mark is of each event)");
}

static void test_web(void)
{
    wait_ms(1000);
    web_tap(80, 300);
    expect(one_tap() == 0, "the web tap (a virtual finger of 90 ms) gives a tap, and it is refused");
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "a finger on the panel right after a web tap is taken");
}

static void test_late_read(void)
{
    // the app task is busy (an app opens, a slow frame): it reads the events 2 s after the web tap
    wait_ms(1000);
    web_tap(80, 300);
    wait_ms(2000);
    expect(one_tap() == 0, "a web tap that the app task reads 2 s later is refused");
    // a web tap, then a finger 1 s later; the app task reads both 2.5 s after the first
    web_tap(80, 300);
    wait_ms(800);
    panel_tap(80, 300, 60);
    wait_ms(1400);
    int t[4];
    const int n = read_taps(t);
    expect(n == 2 && t[0] == 0 && t[1] == 1, "two taps read late: the web tap refused, the finger taken");
}

static void test_mixed_touch(void)
{
    // a finger on the panel, and a virtual finger at the same point for 30 ms in the middle of the touch
    wait_ms(1000);
    finger_down(80, 300);
    wait_ms(60);
    input_virtual_finger(80, 300, now_ms() + 30);
    wait_ms(80);
    finger_up();
    reports(8);
    expect(one_tap() == 0, "a touch with a virtual finger in one of its reports is not by a finger");
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "the next touch of the finger is taken");
}

static void test_finger2(void)
{
    // the console "finger2 x y 90": the second virtual finger alone gives a tap
    wait_ms(1000);
    input_virtual_finger2(30, 500, now_ms() + 90);
    wait_ms(200);
    expect(one_tap() == 0, "the tap of the second virtual finger is refused");
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "a finger after the second virtual finger is taken");
}

static void test_long_finger(void)
{
    // the console "finger x y 5000": a long press (no tap); then "finger up"
    wait_ms(1000);
    input_virtual_finger(80, 300, now_ms() + 5000);
    wait_ms(3000);
    input_event_t ev;
    int longs = 0, taps = 0, fingers = 0;
    while (input_get(&ev)) {
        longs += ev.kind == IN_LONG;
        taps += ev.kind == IN_TAP;
        fingers += !input_injected(&ev);
    }
    expect(longs == 1 && taps == 0 && fingers == 0, "a long virtual finger: one long press, no tap, no finger event");
    input_virtual_finger(0, 0, 0);
    wait_ms(100);
    (void)one_tap();
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "a finger after a long virtual finger is taken");
}

// the pull zone of the panel (the top middle)
static const input_zone_t ZONE = {.on = true,
                                  .mode = ZONE_HOLD,
                                  .x0 = 40,
                                  .y0 = 0,
                                  .x1 = 132,
                                  .y1 = 60,
                                  .dx = 0,
                                  .dy = 1,
                                  .pull_px = 30,
                                  .side_px = 20};

static void test_zone(void)
{
    // a finger in the pull zone waits for the decision; the reports that wait keep their mark
    input_pull_zone(&ZONE);
    wait_ms(1000);
    web_tap(86, 20);
    expect(one_tap() == 0, "a web tap in the pull zone (the events after the hold) is refused");
    panel_tap(86, 20, 60);
    expect(one_tap() == 1, "a finger in the pull zone is taken");
    const input_zone_t off = {.on = false};
    input_pull_zone(&off);
    wait_ms(100);
}

static void test_zone_replay(void)
{
    // a finger is held in the pull zone (the event machine waits), a web tap comes in the hold, and then the held
    // finger pulls: the reports that waited go through the event machine, and the tap of the web keeps its mark
    input_pull_zone(&ZONE);
    wait_ms(1000);
    finger_down(86, 10);
    reports(2);
    input_virtual_finger(80, 300, now_ms() + 90);
    reports(8);
    finger_down(86, 50);  // (40 px down, 0.1 s after the finger came down: the pull)
    reports(6);
    int t[4];
    int n = read_taps(t);
    expect(n == 1 && t[0] == 0, "a web tap in the hold of a finger that pulls: the tap is not by a finger");
    finger_up();
    reports(8);
    expect(one_tap() == -1, "the finger of the pull gives no event");
    // the same, but the held finger stays still: the hold ends as a normal finger, and the touch of its events gets the
    // mark of the web finger (a touch with a virtual finger in one of its reports is not by a finger)
    wait_ms(1000);
    finger_down(86, 10);
    reports(2);
    input_virtual_finger(80, 300, now_ms() + 90);
    reports(20);
    finger_up();
    reports(8);
    input_event_t ev;
    int events = 0, fingers = 0, downs = 0;
    while (input_get(&ev)) {
        events++;
        downs += ev.kind == IN_DOWN;
        fingers += ev.kind != IN_DOWN && !input_injected(&ev);
    }
    expect(downs == 1 && events >= 3 && fingers == 0,
           "a web finger in the hold of a finger that stays: each event after the IN_DOWN is not by a finger");
    const input_zone_t off = {.on = false};
    input_pull_zone(&off);
    wait_ms(100);
}

static void test_events_of_others(void)
{
    // input_injected(ev) of an event that input_get() did not give, of a key, and of an event with the app clock
    wait_ms(1000);
    panel_tap(80, 300, 60);
    input_event_t ev, tap = {0};
    while (input_get(&ev)) {
        if (ev.kind == IN_TAP) {
            tap = ev;
        }
    }
    expect(tap.kind == IN_TAP && !input_injected(&tap), "the tap of a finger, as input_get() gave it");
    input_event_t other = tap;
    other.t_ms += 1;
    expect(input_injected(&other), "an event that input_get() did not give is not by a finger");
    const input_event_t key = {.kind = IN_KEY, .x = INK_ENTER, .y = 0, .t_ms = tap.t_ms};
    expect(input_injected(&key), "a key is not by a finger on the panel");
    input_time_shift(5000);  // (main.c: the app was frozen 5 s under the panel; the app gets t_ms - 5000)
    panel_tap(80, 300, 60);
    tap.kind = IN_DOWN;
    while (input_get(&ev)) {
        if (ev.kind == IN_TAP) {
            tap = ev;
        }
    }
    input_event_t app = tap;
    app.t_ms -= 5000;
    expect(tap.kind == IN_TAP && !input_injected(&app), "the tap of a finger with the app clock is by a finger");
    input_time_shift(0);
}

// the consent helper of input.h with the real events: the result of each IN_TAP (-1 = none)
static int consent_read(input_consent_t *c)
{
    input_event_t ev;
    int r = -1;
    while (input_get(&ev)) {
        const int k = input_consent_event(c, &ev);
        if (ev.kind == IN_TAP) {
            r = k;
        }
    }
    return r;
}

static void test_consent(void)
{
    input_consent_t c;
    wait_ms(1000);
    input_consent_reset(&c);
    panel_tap(80, 300, 60);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a tap before the question showed: no answer");
    const int64_t t = now_ms();
    input_consent_shown(&c, t);
    wait_ms(100);
    panel_tap(80, 300, 60);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a touch that came down 0.1 s after the question: no answer");
    wait_ms(INPUT_ARM_MS);
    panel_tap(80, 300, 60);
    expect(consent_read(&c) == INPUT_CONSENT_OK, "a touch that came down after INPUT_ARM_MS: the answer");
    console_tap();
    expect(consent_read(&c) == INPUT_CONSENT_NOT_FINGER, "a console tap: not by a finger");
    web_tap(80, 300);
    wait_ms(2000);
    expect(consent_read(&c) == INPUT_CONSENT_NOT_FINGER, "a web tap read 2 s later: not by a finger");
    // a finger that is down when the question comes (its IN_DOWN went to the app), then it lifts on the button
    wait_ms(1000);
    finger_down(80, 300);
    wait_ms(60);
    (void)consent_read(&c);  // (the IN_DOWN, before the question)
    input_consent_reset(&c);
    input_consent_shown(&c, now_ms());
    wait_ms(100);
    finger_up();
    reports(8);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a finger that was down when the question came: no answer");
    // a console IN_DOWN in the middle of a touch of a finger: the touch does not answer
    wait_ms(1000);
    finger_down(80, 300);
    wait_ms(60);
    const input_event_t d = {.kind = IN_DOWN, .x = 80, .y = 300, .t_ms = now_ms()};
    input_inject(&d);
    wait_ms(40);
    finger_up();
    reports(8);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a console IN_DOWN in a touch of a finger: no answer");
}

// another part of the OS takes the events now (main.c frame_saver(), the locate screen): the question does not get them
static void drain(void)
{
    input_event_t ev;
    while (input_get(&ev)) {
    }
}

static void test_consent_own_touch(void)
{
    // the question shows; the user rests a finger on ALLOW (a long press: IN_DOWN, IN_LONG, IN_UP, no IN_TAP)
    input_consent_t c;
    wait_ms(1000);
    input_consent_reset(&c);
    input_consent_shown(&c, now_ms());
    wait_ms(INPUT_ARM_MS + 200);
    finger_down(86, 320);
    wait_ms(LONG_PRESS_MS + 150);
    finger_up();
    reports(8);
    expect(consent_read(&c) == -1, "a long press gives no tap");
    // 17 s later a touch whose IN_DOWN goes to another part; its IN_UP and IN_TAP come to the question
    wait_ms(17000);
    finger_down(86, 320);
    reports(3);
    drain();
    reports(3);
    finger_up();
    reports(8);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a long press, then a tap with no IN_DOWN: no answer");
    // the wake from the screen saver: frame_saver() reads the IN_DOWN and empties the queue (input_flush), and the IN_UP
    // and the IN_TAP of the touch come in the next frame
    finger_down(86, 320);
    wait_ms(LONG_PRESS_MS + 150);
    finger_up();
    reports(8);
    (void)consent_read(&c);
    wait_ms(17000);
    finger_down(86, 320);
    reports(3);
    drain();
    input_flush();
    reports(3);
    finger_up();
    reports(8);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "the wake from the screen saver after a long press: no answer");
    // a drag on ALLOW (no tap), then at once a tap whose IN_DOWN goes to another part (all in less than LONG_PRESS_MS)
    wait_ms(1000);
    finger_down(80, 300);
    reports(3);
    finger_down(80, 340);  // (40 px: more than the slop of a tap)
    reports(3);
    finger_up();
    reports(5);
    expect(consent_read(&c) == -1, "a drag gives no tap");
    finger_down(80, 300);
    reports(2);
    drain();
    reports(2);
    finger_up();
    reports(5);
    expect(consent_read(&c) == INPUT_CONSENT_EARLY, "a drag, then a tap with no IN_DOWN 0.1 s later: no answer");
    // the next touch of the finger answers
    wait_ms(200);
    panel_tap(80, 300, 60);
    expect(consent_read(&c) == INPUT_CONSENT_OK, "after these, the next touch of the finger answers");
}

int main(void)
{
    input_init();
    expect(s_task != NULL, "input_init starts the touch task");
    if (!s_task) {
        printf("INPUT GATE TESTS FAILED\n");
        return 1;
    }
    wait_ms(100);
    panel_tap(80, 300, 60);
    expect(one_tap() == 1, "a tap of a finger on the panel is by a finger");
    test_console();
    test_web();
    test_late_read();
    test_mixed_touch();
    test_finger2();
    test_long_finger();
    test_zone();
    test_zone_replay();
    test_events_of_others();
    test_consent();
    test_consent_own_touch();
    printf("INPUT GATE TESTS %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
