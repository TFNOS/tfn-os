#pragma once

// The steps of one firmware update over the network (ota_flow.c): the checks before the erase, the first block, the
// erase, the write of each block, the board tag, the running hash and the trailer copy, the signature check
// (ota_sig.h), the end and the boot slot. Pure C: the board (ota.c) and the fake flash of the unit test
// (tests/unit/test_ota_flow.c) give the functions of ota_flow_io_t. The texts of the answer stay in ota.c.

#include <stdbool.h>
#include <stdint.h>
#include "ota_sig.h"

#define OTA_FLOW_BLOCK    8192   // the bytes of one receive
#define OTA_FLOW_KEEP     40     // the bytes kept from the last block: a tag across two blocks
#define OTA_FLOW_MIN      65536  // the smallest file that can be an image
#define OTA_FLOW_DESC_VER 0x30   // the version in the image: esp_app_desc_t (at 0x20) + 16
#define OTA_FLOW_TAG_PFX  14     // "@ARCADE-BOARD:" (the start of each board tag)

// recv(): the codes of a fault (0 = the sender closed the connection)
#define OTA_FLOW_RECV_NONE (-1000)  // no data for 60 s
#define OTA_FLOW_RECV_ERR  (-1001)  // socket errors

// step(): the steps that the panel shows
enum {
    OTA_STEP_ERASE,     // the first block is good: the erase of the slot starts
    OTA_STEP_WRITE,     // the erase is done: the writes start
    OTA_STEP_PROGRESS,  // a block is written (done bytes)
};

typedef enum {
    OTA_FLOW_OK,     // the update is written, the boot slot is set
    OTA_FLOW_SLOT,   // the board has no second app slot
    OTA_FLOW_BUSY,   // an update runs
    OTA_FLOW_SIZE,   // the size is not the size of an image for this slot
    OTA_FLOW_CLAIM,  // another update started at the same time
    OTA_FLOW_RAM,    // the trial of the hash contexts failed (too little internal RAM)
    OTA_FLOW_MEM,    // no memory for the work buffers
    OTA_FLOW_RECV,   // a receive fault (out.recv: 0, OTA_FLOW_RECV_NONE or OTA_FLOW_RECV_ERR)
    OTA_FLOW_IMAGE,  // the first byte is not 0xE9: not an image
    OTA_FLOW_BEGIN,  // the erase of the slot failed (out.err)
    OTA_FLOW_WRITE,  // a flash write failed (out.err)
    OTA_FLOW_TAG,    // no tag of this board kind (out.other: the kind of another tag)
    OTA_FLOW_SIG,    // the signature check refused the image (ENFORCE only; out.sig)
    OTA_FLOW_END,    // the end check of the image failed (out.err)
    OTA_FLOW_BOOT,   // the board could not set the boot slot (out.err)
} ota_flow_res_t;

// The work memory of one update (the board puts it in PSRAM)
typedef struct {
    char buf[OTA_FLOW_BLOCK + OTA_FLOW_KEEP];
    uint8_t trailer[OTA_SIG_TRAILER];  // the last OTA_SIG_TRAILER bytes of the file
} ota_flow_mem_t;

// The functions of the board. ctx = the value that the caller gives to ota_flow_run().
typedef struct {
    bool (*busy)(void *ctx);     // true: an update runs
    bool (*claim)(void *ctx);    // true: this update now owns the update slot
    void (*release)(void *ctx);  // the end of the claim (after a fault)
    bool (*trial)(void *ctx);    // true: the hash contexts that the update and its end check need can start
    void (*wake)(void *ctx);     // the screen on (the network is slow with the screen saver)
    ota_flow_mem_t *(*alloc)(void *ctx);
    void (*free_mem)(void *ctx, ota_flow_mem_t *m);
    // the next bytes of the body into dst (want > 0): > 0 the bytes, 0 the sender closed, < 0 OTA_FLOW_RECV_*
    int (*recv)(void *ctx, char *dst, int want);
    void (*step)(void *ctx, int step, int done, int total);
    int (*begin)(void *ctx, int total);             // the erase: 0 = OK, else the error number
    int (*write)(void *ctx, const char *p, int n);  // 0 = OK, else the error number
    void (*abort)(void *ctx);                       // the end of an update that failed after begin()
    bool (*hash_start)(void *ctx);
    bool (*hash_update)(void *ctx, const uint8_t *p, int n);
    bool (*hash_finish)(void *ctx, uint8_t out[32]);  // also on a fault, the hash context is free after it
    void (*hash_abort)(void *ctx);
    // the result of the signature check (accepted: the update goes on)
    void (*on_sig)(void *ctx, const ota_sig_result_t *r, bool accepted);
    int (*end)(void *ctx);       // the end check of the image: 0 = OK (the handle is closed also on a fault)
    int (*set_boot)(void *ctx);  // 0 = OK
} ota_flow_io_t;

typedef ota_sig_res_t (*ota_flow_check_t)(const uint8_t *tr, size_t n, uint32_t total, const uint8_t hash[32],
                                          const ota_sig_want_t *want, const ota_sig_crypto_t *crypto,
                                          ota_sig_result_t *out);

typedef struct {
    const char *tag;         // the board tag of this board: "@ARCADE-BOARD:<kind>@"
    int slot_size;           // the bytes of the slot that the update writes (0: no second slot)
    ota_sig_mode_t mode;     // the mode of the signature check
    ota_flow_check_t check;  // ota_sig_check, or NULL: no hash, no trailer copy, no check (mode OFF)
    const ota_sig_crypto_t *crypto;
    ota_sig_want_t want;  // the key list, the kind, minsv (the flow sets the version from the image)
} ota_flow_cfg_t;

typedef struct {
    ota_flow_res_t res;
    int total;       // the bytes of the body
    int done;        // the bytes written (or received before the erase)
    int err;         // the error number of begin / write / end / set_boot
    int recv;        // the code of a receive fault
    char ver[33];    // the version of the new image
    char other[16];  // the kind of a tag of another board
    bool checked;    // the signature check ran: sig has its result
    ota_sig_result_t sig;
} ota_flow_out_t;

// One update of total bytes. The result is also in out->res.
ota_flow_res_t ota_flow_run(const ota_flow_io_t *io, void *ctx, const ota_flow_cfg_t *cfg, int total,
                            ota_flow_out_t *out);
// The board tags in buf[0, n): own = the tag of this board (tag), other = the kind of another board (empty: none)
void ota_flow_scan_tags(const char *tag, const char *buf, int n, bool *own, char other[16]);
