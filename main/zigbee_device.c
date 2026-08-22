#include "zigbee_device.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_endpoint.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_analog_output.h"
#include "zcl/esp_zigbee_zcl_basic.h"
#include "zcl/esp_zigbee_zcl_identify.h"
#include "zcl/esp_zigbee_zcl_command.h"

static const char *TAG = "ZIGBEE_DEVICE";

static void zcl_string_from_cstr(uint8_t *out, size_t out_size, const char *in)
{
    size_t len = strlen(in);
    if (len > 255) {
        len = 255;
    }
    if (out_size < len + 1) {
        len = out_size - 1;
    }
    out[0] = (uint8_t)len;
    if (len > 0) {
        memcpy(out + 1, in, len);
    }
}

zigbee_analog_device_t g_temp_device = {
    .current_value = 0.0f,
    .min_value = -100.0f,
    .max_value = 100.0f,
    .resolution = 0.1f,
    .description = "Temperature",
    .endpoint = TEMP_ENDPOINT_NUMBER,
};

zigbee_analog_device_t g_humid_device = {
    .current_value = 0.0f,
    .min_value = 0.0f,
    .max_value = 100.0f,
    .resolution = 0.1f,
    .description = "Humidity",
    .endpoint = HUMID_ENDPOINT_NUMBER,
};

static esp_err_t create_analog_endpoint(esp_zb_ep_list_t *ep_list,
                                        uint8_t endpoint,
                                        zigbee_analog_device_t *device)
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    if (!cluster_list) {
        return ESP_ERR_NO_MEM;
    }

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    if (!basic) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t manufacturer_name[32] = {0};
    uint8_t model_identifier[32] = {0};
    zcl_string_from_cstr(manufacturer_name, sizeof(manufacturer_name), "Espressif");
    zcl_string_from_cstr(model_identifier, sizeof(model_identifier), "EinkMeteoReceiver");

    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                                   manufacturer_name));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                                   model_identifier));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic,
                                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&identify_cfg);
    if (!identify) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, identify,
                                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    if (endpoint == TEMP_ENDPOINT_NUMBER || endpoint == HUMID_ENDPOINT_NUMBER) {
        esp_zb_analog_output_cluster_cfg_t analog_output_cfg = {
            .out_of_service = false,
            .present_value = device->current_value,
            .status_flags = ESP_ZB_ZCL_ANALOG_OUTPUT_STATUS_FLAG_NORMAL,
        };
        esp_zb_attribute_list_t *analog_output = esp_zb_analog_output_cluster_create(&analog_output_cfg);
        if (!analog_output) {
            return ESP_ERR_NO_MEM;
        }

        float min_value = device->min_value;
        float max_value = device->max_value;
        float resolution = device->resolution;
        uint32_t app_type = 0;
        uint8_t description_attr[32] = {0};
        zcl_string_from_cstr(description_attr, sizeof(description_attr), device->description);

        if (strcmp(device->description, "Temperature") == 0) {
            app_type = ESP_ZB_ZCL_AO_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AO_APP_TYPE_TEMPERATURE, 0x0000);
        } else if (strcmp(device->description, "Humidity") == 0) {
            app_type = ESP_ZB_ZCL_AO_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AO_APP_TYPE_HUMIDITY, 0x0000);
        }

        ESP_ERROR_CHECK(esp_zb_analog_output_cluster_add_attr(
            analog_output, ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_DESCRIPTION_ID, description_attr));
        ESP_ERROR_CHECK(esp_zb_analog_output_cluster_add_attr(
            analog_output, ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_MIN_PRESENT_VALUE_ID, &min_value));
        ESP_ERROR_CHECK(esp_zb_analog_output_cluster_add_attr(
            analog_output, ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_MAX_PRESENT_VALUE_ID, &max_value));
        ESP_ERROR_CHECK(esp_zb_analog_output_cluster_add_attr(
            analog_output, ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_RESOLUTION_ID, &resolution));
        ESP_ERROR_CHECK(esp_zb_analog_output_cluster_add_attr(
            analog_output, ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_APPLICATION_TYPE_ID, &app_type));

        ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_output_cluster(
            cluster_list, analog_output, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    }

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = endpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = 0x0000,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config));
    ESP_LOGI(TAG, "Added endpoint %d with '%s'", endpoint, device->description);
    return ESP_OK;
}

esp_err_t zigbee_analog_devices_register(void)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    if (!ep_list) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(create_analog_endpoint(ep_list, TEMP_ENDPOINT_NUMBER, &g_temp_device));
    ESP_ERROR_CHECK(create_analog_endpoint(ep_list, HUMID_ENDPOINT_NUMBER, &g_humid_device));
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));
    ESP_LOGI(TAG, "Device registered with 2 endpoints");
    return ESP_OK;
}

esp_err_t zigbee_analog_set_value(uint8_t endpoint, float value)
{
    zigbee_analog_device_t *device = NULL;
    if (endpoint == TEMP_ENDPOINT_NUMBER) {
        device = &g_temp_device;
    } else if (endpoint == HUMID_ENDPOINT_NUMBER) {
        device = &g_humid_device;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    if (value < device->min_value) value = device->min_value;
    if (value > device->max_value) value = device->max_value;
    device->current_value = value;
    ESP_LOGI(TAG, "Set %s (ep %d) = %.1f", device->description, endpoint, value);
    return ESP_OK;
}

float zigbee_analog_get_value(uint8_t endpoint)
{
    if (endpoint == TEMP_ENDPOINT_NUMBER) return g_temp_device.current_value;
    if (endpoint == HUMID_ENDPOINT_NUMBER) return g_humid_device.current_value;
    return 0.0f;
}

esp_err_t zigbee_zcl_core_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                          const void *message)
{
    if (callback_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        return ESP_OK;
    }

    const esp_zb_zcl_set_attr_value_message_t *msg = message;
    if (!msg || msg->info.status != ESP_ZB_ZCL_STATUS_SUCCESS || !msg->attribute.data.value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_OUTPUT &&
        msg->attribute.id == ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID) {
        float value = 0.0f;

        if (msg->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_SINGLE) {
            value = *(float *)msg->attribute.data.value;
        } else if (msg->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
            value = (float)(*(int16_t *)msg->attribute.data.value) / 100.0f;
        } else if (msg->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
            value = (float)(*(uint16_t *)msg->attribute.data.value) / 100.0f;
        } else {
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "ZCL write analog output: ep=%d, value=%.2f", msg->info.dst_endpoint, value);
        return zigbee_analog_set_value(msg->info.dst_endpoint, value);
    }

    return ESP_OK;
}
