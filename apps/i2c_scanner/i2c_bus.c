// I2C SCANNER: the bus and its task (see i2c_bus.h). The board part uses the I2C master driver of ESP-IDF (the ABI
// names of the class "bus"); the PC part is a fake bus with the chips of the board and three modules (the PC tests).
// The task: the app task calls the ib_* functions; the bus task "i2cscan" (core 0) does the work on the bus.
// The memory: the file statics; the stack of the task in PSRAM (the task frees it at its end).
#include "i2c_bus.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "i2c_db.h"

static ib_state_t s_ib;  // (the task writes it between two changes of s_seq)
static volatile uint32_t s_seq;
static volatile int s_req;  // the request of the app: IB_SCAN, IB_IDENT, IB_REGS (0 = none)
static volatile uint8_t s_req_addr;
static volatile bool s_live;
static const rfp_i2c_t *s_bus;
static int s_khz = 100;

const char *ib_err(int e)
{
    switch (e) {
    case IB_E_OK:
        return "OK";
    case IB_E_BUS:
        return "THE BUS DID NOT START";
    case IB_E_TASK:
        return "NO MEMORY FOR THE TASK";
    case IB_E_STUCK:
        return "THE BUS IS STUCK: SDA OR SCL STAYS LOW";
    default:
        return "?";
    }
}

static int64_t ms(void)
{
    return esp_timer_get_time() / 1000;
}
static void w_begin(void)
{
    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);
}
static void w_end(void)
{
    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);
}

void ib_get(ib_state_t *o)
{
    for (int i = 0; i < 50; i++) {
        const uint32_t a = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
        if (a & 1) {
            continue;
        }
        *o = s_ib;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s_seq, __ATOMIC_ACQUIRE) == a) {
            return;
        }
    }
    *o = s_ib;
}

void ib_scan(void)
{
    s_req = IB_SCAN;
}
void ib_live(bool on)
{
    s_live = on;
}
void ib_ident(uint8_t a)
{
    s_req_addr = a;
    s_req = IB_IDENT;
}
void ib_regs(uint8_t a)
{
    s_req_addr = a;
    s_req = IB_REGS;
}

// ---------------------------------------------------------------- the work (the same on the board and on the PC)
static bool bus_probe(uint8_t a);
static int bus_read(uint8_t a, uint8_t reg, uint8_t *v);  // 1 = a value, 0 = no answer, < 0 = a bus fault

static void do_scan(void)
{
    uint8_t f[16] = {0};
    const int64_t t0 = ms();
    w_begin();
    s_ib.state = IB_SCAN;
    w_end();
    for (int a = 0x08; a <= 0x77; a++) {
        if (bus_probe((uint8_t)a)) {
            f[a >> 3] |= (uint8_t)(1 << (a & 7));
        }
    }
    w_begin();
    memcpy(s_ib.found, f, sizeof f);
    s_ib.scans++;
    s_ib.t_scan = ms();
    s_ib.scan_ms = (int)(s_ib.t_scan - t0);
    s_ib.state = IB_IDLE;
    w_end();
}

static void do_ident(uint8_t a)
{
    int found = -1;
    for (int i = 0; i < I2C_DB_N && found < 0; i++) {
        const i2c_chip_t *c = &I2C_DB[i];
        if (!c->has_id || a < c->lo || a > c->hi) {
            continue;
        }
        uint8_t v;
        if (bus_read(a, c->idreg, &v) == 1 && v == c->idval) {
            found = i;
        }
    }
    w_begin();
    s_ib.req_addr = a;
    s_ib.ident = found;
    s_ib.req_done++;
    s_ib.state = IB_IDLE;
    w_end();
}

static void do_regs(uint8_t a)
{
    static uint8_t r[256], ok[32];
    memset(ok, 0, sizeof ok);
    for (int i = 0; i < 256; i++) {
        uint8_t v = 0;
        const int k = bus_read(a, (uint8_t)i, &v);
        if (k < 0) {
            break;
        }
        if (k == 1) {
            r[i] = v;
            ok[i >> 3] |= (uint8_t)(1 << (i & 7));
        }
    }
    w_begin();
    s_ib.req_addr = a;
    memcpy(s_ib.regs, r, sizeof r);
    memcpy(s_ib.regs_ok, ok, sizeof ok);
    s_ib.req_done++;
    s_ib.state = IB_IDLE;
    w_end();
}

// one round of the task: a request, or a scan of LIVE
static int64_t s_next_live;
static void round_(void)
{
    const int rq = s_req;
    if (rq) {
        s_req = 0;
        const uint8_t a = s_req_addr;
        w_begin();
        s_ib.state = rq;
        w_end();
        if (rq == IB_SCAN) {
            do_scan();
        } else if (rq == IB_IDENT) {
            do_ident(a);
        } else if (rq == IB_REGS) {
            do_regs(a);
        }
        s_next_live = ms() + 1500;
        return;
    }
    if (s_live && ms() >= s_next_live) {
        do_scan();
        s_next_live = ms() + 1500;
    }
}

#ifdef ESP_PLATFORM
// ================================================================ the board: the I2C master driver and a task
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t s_h;
static bool s_own;  // the app made the bus (it deletes it)
static TaskHandle_t s_task;
static TaskHandle_t volatile s_orphan;
static volatile bool s_quit, s_alive;

static bool bus_probe(uint8_t a)
{
    const esp_err_t e = i2c_master_probe(s_h, a, 20);
    if (e == ESP_ERR_TIMEOUT) {
        w_begin();
        s_ib.err = IB_E_STUCK;
        w_end();
    }
    return e == ESP_OK;
}

static int bus_read(uint8_t a, uint8_t reg, uint8_t *v)
{
    i2c_device_config_t dc;
    memset(&dc, 0, sizeof dc);
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = a;
    dc.scl_speed_hz = (uint32_t)s_khz * 1000;
    i2c_master_dev_handle_t d;
    if (i2c_master_bus_add_device(s_h, &dc, &d) != ESP_OK) {
        return -1;
    }
    const esp_err_t e = i2c_master_transmit_receive(d, &reg, 1, v, 1, 30);
    i2c_master_bus_rm_device(d);
    return e == ESP_OK ? 1 : e == ESP_ERR_TIMEOUT ? -1 : 0;
}

static void task(void *arg)
{
    (void)arg;
    while (!s_quit) {
        round_();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_alive = false;
    for (;;) {  // (ib_close deletes the task; a late task deletes itself)
        if (s_orphan) {
            TaskHandle_t me = s_orphan;
            s_orphan = NULL;
            vTaskDeleteWithCaps(me);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool ib_open(const rfp_i2c_t *bus, int khz)
{
    ib_close();
    if (s_alive) {
        return false;  // (the task of an earlier open still ends)
    }
    memset(&s_ib, 0, sizeof s_ib);
    s_ib.ident = -1;
    s_bus = bus;
    s_khz = khz;
    s_h = NULL;
    s_own = false;
    if (bus->shared) {
        if (i2c_master_get_bus_handle((i2c_port_num_t)bus->port, &s_h) != ESP_OK) {
            s_h = NULL;
        }
    } else {
        i2c_master_bus_config_t bc;
        memset(&bc, 0, sizeof bc);
        bc.i2c_port = bus->port;
        bc.sda_io_num = bus->conn.data_gpio;
        bc.scl_io_num = bus->conn.tx_gpio;
        bc.clk_source = I2C_CLK_SRC_DEFAULT;
        bc.glitch_ignore_cnt = 7;
        bc.flags.enable_internal_pullup = true;  // (weak: a module with its own pull-ups is better)
        if (i2c_new_master_bus(&bc, &s_h) != ESP_OK) {
            s_h = NULL;
        } else {
            s_own = true;
        }
    }
    if (!s_h) {
        s_ib.err = IB_E_BUS;
        s_ib.state = IB_FAIL;
        return false;
    }
    s_quit = false;
    s_alive = true;
    s_req = IB_SCAN;
    if (xTaskCreatePinnedToCoreWithCaps(task, "i2cscan", 4096, NULL, 2, &s_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        s_alive = false;
        s_ib.err = IB_E_TASK;
        s_ib.state = IB_FAIL;
        if (s_own) {
            i2c_del_master_bus(s_h);
        }
        s_h = NULL;
        return false;
    }
    return true;
}

void ib_close(void)
{
    if (s_task) {
        s_quit = true;
        for (int i = 0; i < 100 && s_alive; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!s_alive) {
            vTaskDeleteWithCaps(s_task);
        } else {
            s_orphan = s_task;
        }
        s_task = NULL;
    }
    if (s_own && s_h && !s_alive) {
        i2c_del_master_bus(s_h);
    }
    s_h = NULL;
    s_own = false;
}

#else
// ================================================================ the PC: a fake bus (the chips of the board, a
// BME280 at 0x76 on a free bus, an OLED at 0x3C, an EEPROM at 0x50); the task is a call in ib_get
#include "rfpulse/rfp_board.h"
static bool s_pc_open;
static bool s_pc_extra = true;  // (the PC tests turn the three modules off and on)
void ib_pc_modules(bool on)
{
    s_pc_extra = on;
}

static bool bus_probe(uint8_t a)
{
    if (!s_bus) {
        return false;
    }
    for (const rfp_chip_t *c = s_bus->chips; c && c->addr; c++) {
        if (c->addr == a) {
            return true;
        }
    }
    return s_pc_extra && (a == 0x76 || a == 0x3C || a == 0x50);
}

static int bus_read(uint8_t a, uint8_t reg, uint8_t *v)
{
    if (!bus_probe(a)) {
        return 0;
    }
    if (a == 0x76) {
        *v = reg == 0xD0 ? 0x60 : (uint8_t)(reg * 7 + 3);
    } else if (a == 0x6B) {
        *v = reg == 0x00 ? 0x05 : (uint8_t)reg;
    } else if (a == 0x69) {
        *v = reg == 0x00 ? 0x24 : (uint8_t)(reg ^ 0x5A);
    } else if (a == 0x3C) {
        return reg < 0x40 ? (*v = 0x43, 1) : 0;
    } else {
        *v = (uint8_t)(reg + a);
    }
    return 1;
}

bool ib_open(const rfp_i2c_t *bus, int khz)
{
    memset(&s_ib, 0, sizeof s_ib);
    s_ib.ident = -1;
    s_bus = bus;
    s_khz = khz;
    s_pc_open = true;
    s_req = IB_SCAN;
    return true;
}
void ib_close(void)
{
    s_pc_open = false;
}
// (the PC: the work of the task runs when the app reads the state)
void ib_pc_round(void)
{
    if (s_pc_open) {
        round_();
    }
}
#endif
