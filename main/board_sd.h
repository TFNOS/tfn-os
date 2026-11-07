// The microSD card code of the boards (inside the OS; the apps use board.h). board_sd.c has the parts that are the
// same on all boards with a card slot: the lock, the mount state, board_sd_check/mounted/info/mount/format. Each
// board file has the mount with its own bus and pins.
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

// board_init() calls it before the first use of the card.
void board_sd_init(void);

// The board part: mount the card at BOARD_SD_ROOT with the bus and the pins of the board. The result of the
// esp_vfs_fat_*_mount call (it sets *card). board_sd.c holds the lock while it runs.
esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card);

// A weak function (board_sd.c returns true): false when the board knows that the slot has no card (the Watcher has
// a card detect line). Then a mount does not try.
bool board_sd_card_in(void);

// A weak function (board_sd.c does nothing): after an unmount and after a failed mount. The CoreS3 sets its card CS
// line high there, because the card shares the bus with the LCD.
void board_sd_card_released(void);

// The mount of a card on the SDMMC host in the 1-bit mode at 20 MHz, with the internal pull-ups (the 3.49 and the
// AMOLED-1.8).
esp_err_t board_sd_mount_sdmmc_1bit(int clk, int cmd, int d0, const esp_vfs_fat_mount_config_t *mc,
                                    sdmmc_card_t **card);

// After a mount on the SDMMC host: the data read timeout of the host becomes 500 ms (the driver sets 100 ms; a busy
// card after many writes can need more for one read). Only the ESP32-S3 and the ESP32-P4: on other chips no change.
void board_sd_data_timeout(const sdmmc_card_t *card);

// Before a restart: the card goes idle (an unmount ends its last command). It waits 300 ms for the lock at most.
// The Tab5 calls it from a shutdown handler: a restart does not cut the power of its card.
void board_sd_off_at_restart(void);
