// Tab5Boot: the request "start this app slot one time". It is in the custom part of the RTC memory that the
// bootloader keeps (CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC, 16 bytes). An app writes it and restarts; the bootloader
// reads it, clears it and starts the slot. With no request, the bootloader starts the boot menu (factory).
// The source is Tab5Boot/main/boot_req.h (the Tab5 boot project); this is a copy for TFN OS. Keep the two the same.
#pragma once
#include <stdint.h>

#define BOOT_REQ_MAGIC 0x54354254u  // "T5BT"

typedef struct {
    uint32_t magic;  // BOOT_REQ_MAGIC
    uint32_t slot;   // 0..15 = the OTA slot (tfn_a 0, tfn_b 1, dos 2)
    uint32_t check;  // ~slot
    uint32_t spare;
} boot_req_t;
