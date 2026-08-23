idf_component_register(SRCS "main.c" "zigbee_device.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp-zigbee-lib nvs_flash driver esp_timer)
