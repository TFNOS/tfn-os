/* THE NATIVE RADIO (main/rf_native.c): the dump engine of the receiver writes into the fixed block 0x3FCD0000..
   0x3FCE0000. No static data or code of the firmware can be in it (the reserve of the block would overlap them). */
ASSERT(_bss_end <= 0x3fcd0000, "rf_native: the static RAM (.bss) reaches the radio block 0x3FCD0000")
ASSERT(_data_end <= 0x3fcd0000, "rf_native: the static RAM (.data) reaches the radio block 0x3FCD0000")
ASSERT(_iram_end <= 0x403c0000, "rf_native: the IRAM reaches the radio block (0x403C0000 = 0x3FCD0000)")
