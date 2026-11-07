#pragma once
#include <stdbool.h>
// FILES (app_files.c): a function that the app needs from the board files.
// The format of the SD card (CARD TOOLS > FORMAT): a new, empty FAT file system on the card (esp_vfs_fat_sdcard_format).
// It must also work when the card does not mount (a new card, or a card with another file system). After it, the card
// is mounted at BOARD_SD_ROOT. True: the format is done. app_files.c has a weak copy that returns false (then FORMAT
// says that the firmware has no format); a board file gives the real one.
bool board_sd_format(void);
