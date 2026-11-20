#pragma once
// THE TFN COMPANIONS: small ESP32 boards with no screen (the firmware in companion/ of the repo) that are the "hands" of
// this board: an IR receiver, an IR LED, 315 / 433 MHz modules on their pins. TFN OS keeps the paired companions
// (/data/companions.bin: their names and pair keys) and speaks their protocol (libs/tfncomp). Each app that uses them
// (COMPANIONS: pair, wiring, live test; REMOTE: IR remotes; later the RF apps) shares this service:
//   comp_open() at the enter of the app, comp_poll() in each update(), comp_close() at the leave;
//   the list and the requests: tcm_count(), tcm_get(), tcm_online(), tcm_capture(), tcm_send() ... (tfncomp/tc_master.h);
//   the IR plan of a button: tc_ir_plan() (tfncomp/tc_ir.h).
// With no open, the service does nothing: no radio, no task, no memory.
#include <stdbool.h>
#include "tfncomp/tc_ir.h"
#include "tfncomp/tc_master.h"

// Start the radio frames of the companions (ESP-NOW on the channel of the WiFi network) and read the paired list.
// False: no radio.
bool comp_open(void);
// Each frame of the app: the received frames, the retries, the pings
// (it also sounds a change: a companion comes online = rising notes, goes offline = a low falling note)
void comp_poll(void);
// Stop the radio (the list is saved)
void comp_close(void);
// The name of this board for a pairing ("TFN 1A2B")
const char *comp_my_name(void);
// The first companion that has a module of this role in its wiring (TC_R_*), online first; -1: none.
// *pin = its GPIO.
int comp_with_role(int role, int *pin);

// THE NEAR COMPANIONS (the OS, each frame with the screen on): when no app uses the radio, the board listens for
// 1.2 s each 15 s for the HELLO of a companion (a companion with no master online sends one each 0.5 s on the channel
// of its master). A paired companion that comes near (not heard for 50 s) gives a notice and a rising sound; a new
// companion with no master gives a notice (a tap opens COMPANIONS at ADD) and a lower sound. Only on a board that has
// the file of its companions, and with the store key comp_alerts on (the default; COMPANIONS has the switch).
void comp_bg_tick(int64_t now_ms);
#define COMP_ALERTS_KEY "comp_alerts"

// THE FIRMWARE OF THE COMPANIONS IN THE STORE (the manager or the static store of this board): compfw/index.txt (a
// line "<target> <fw> <size> <sha256>" for each chip; a STORE key signs it: compfw/index.sig) and compfw/<target>.bin.
// A job in the background (one at a time): the index (*fw = the newest version for the target), and with image = true
// the file of the target into PSRAM (its size and its SHA-256 as the index says). target: "esp32", "esp32s3",
// "esp32c3", "esp32c6", "esp32s2". False: a job runs already.
bool comp_fw_start(const char *target, bool image);
// 0: the job runs; 1: done (*fw; with image: *img (free it with heap_caps_free) and *len); -1: msg has the fault
int comp_fw_poll(int *fw, uint8_t **img, int *len, char *msg, int mlen);
