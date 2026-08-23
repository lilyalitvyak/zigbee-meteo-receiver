#ifndef ZIGBEE_DEVICE_H
#define ZIGBEE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEMP_ENDPOINT_NUMBER  1
#define HUMID_ENDPOINT_NUMBER 2

typedef struct {
    float current_value;
    float min_value;
    float max_value;
    float resolution;
    char description[32];
    uint8_t endpoint;
} zigbee_analog_device_t;

extern zigbee_analog_device_t g_temp_device;
extern zigbee_analog_device_t g_humid_device;

esp_err_t zigbee_analog_devices_register(void);
esp_err_t zigbee_analog_set_value(uint8_t endpoint, float value);
float zigbee_analog_get_value(uint8_t endpoint);
void zigbee_remote_values_reset(void);
bool zigbee_remote_values_ready(void);
bool zigbee_take_deep_sleep_request(void);
void zigbee_command_send_status_cb(esp_zb_zcl_command_send_status_message_t message);
esp_err_t zigbee_zcl_core_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);

#ifdef __cplusplus
}
#endif

#endif
