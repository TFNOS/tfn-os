// The AXP2101 power chip (PMIC) of the CoreS3, the AMOLED-1.8 and the AMOLED-1.75C (inside the OS): the register
// names and the functions that are the same on these boards (pmic_axp2101.c). The board files keep the rails, the
// power key and the power off of each board.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#define AXP_ADDR 0x34  // the 7-bit I2C address

#define AXP_CHIP_ID   0x03  // 0x4A
#define AXP_PMU_CFG   0x10  // bit 0: power off
#define AXP_DCDC_UVP  0x23  // bits 0..4: power off when a DCDC output is too low
#define AXP_PKEY_TIME 0x27
#define AXP_ADC_EN    0x30
#define AXP_IRQ_ST0   0x48  // IRQ status 0..2. Write 1 to clear a bit.
#define AXP_IRQ_ST1   0x49
#define AXP_IRQ_ST2   0x4A
#define AXP_CHGLED    0x69
#define AXP_LDO_EN    0x90  // bit 0..3 = ALDO1..4, bit 4..5 = BLDO1..2, bit 7 = DLDO1
#define AXP_ALDO1_V   0x92  // ALDO1..4 = 0x92..0x95, DLDO1 = 0x99: (mV - 500) / 100
#define AXP_ALDO2_V   0x93
#define AXP_ALDO3_V   0x94
#define AXP_ALDO4_V   0x95
#define AXP_DLDO1_V   0x99
#define AXP_MV(mv)    (((mv) - 500) / 100)

// The first transfer after a reset can fail (seen on a board after a flash): up to 5 reads of the chip ID, 10 ms
// apart. An error log when the chip does not answer. True: the chip answered with its ID.
bool axp2101_wait_id(i2c_master_dev_handle_t axp);

// The start of the AMOLED boards: axp2101_wait_id(), then it reads the sources of the last turn-on and turn-off
// (0x20, 0x21) before any change, and sets the power key times, the ADCs and clears the old key events.
void axp2101_init_basic(i2c_master_dev_handle_t axp, uint8_t *pmu_on, uint8_t *pmu_off);

// board_battery() of these boards: the charge in % (-1: no battery), and the charge state and the battery voltage
// in mV. It reads the chip once in 2 s at most.
int axp2101_battery(i2c_master_dev_handle_t axp, bool *charging, int *mv);
// board_ext_power() of these boards: 1 = VBUS good (a charger or a PC), 0 = no VBUS, -1 = no read yet.
int axp2101_vbus(i2c_master_dev_handle_t axp);

// A 14-bit ADC value (the register and the next one), in mV for the voltages. -1 when the read fails.
int axp2101_adc14(i2c_master_dev_handle_t axp, uint8_t reg);
