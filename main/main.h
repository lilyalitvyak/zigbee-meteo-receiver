#pragma once

#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   (1U << 13)
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK 0x07FFF800

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "nvs"

#define ESP_MANUFACTURER_NAME "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER "esp32h2"

#define ESP_ZIGBEE_ZR_CONFIG()                      \
    {                                               \
        .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,  \
        .install_code_policy = false,               \
        .zed_config = {                              \
            .ed_timeout = 6,                         \
            .keep_alive = 3000,                      \
        },                                          \
    }
    
#if CONFIG_SOC_IEEE802154_SUPPORTED
#define ESP_ZIGBEE_PLATFORM_CONFIG()                                 \
    {                                                                \
        .storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME, \
        .radio_config = {                                            \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,              \
        },                                                           \
    }
#else
#warning "The example is not for IEEE 802.15.4-disabled SoC usage, please refer to esp_zigbee_gateway for RCP configuration"
#endif

#define ESP_ZIGBEE_DEFAULT_CONFIG()                      \
    {                                                    \
        .device_config = ESP_ZIGBEE_ZR_CONFIG(),         \
        .platform_config = ESP_ZIGBEE_PLATFORM_CONFIG(), \
    };
    