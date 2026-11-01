idf_component_register(SRCS "boot_diag.c" "battery_monitor.c" "eink_display.c" "epd_1in9.c" "main.c" "zigbee_device.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp-zigbee-lib nvs_flash driver esp_timer esp_adc)
