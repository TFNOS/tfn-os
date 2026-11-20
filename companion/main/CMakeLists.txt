idf_component_register(
    SRCS "main.c" "comp_core.c" "comp_hw.c"
         "../../libs/tfncomp/tfncomp.c" "../../libs/tfncomp/tc_x25519.c" "../../libs/irdecode/irdecode.c"
    INCLUDE_DIRS "." "../../libs"
    REQUIRES app_update esp_adc esp_wifi esp_netif esp_event nvs_flash esp_driver_gpio esp_driver_ledc esp_driver_rmt esp_driver_uart esp_driver_usb_serial_jtag esp_timer)
