#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "esp_zigbee_core.h"
#include "main.h"
#include "zigbee_device.h"

static const char *TAG = "MAIN";

static esp_err_t esp_pm_light_sleep_config(void)
{
    const int current_cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = current_cpu_freq_mhz,
        .min_freq_mhz = current_cpu_freq_mhz,
        .light_sleep_enable = true,
    };
    return esp_pm_configure(&pm_config);
}

static void zigbee_steering_retry(uint8_t mode)
{
    (void)esp_zb_bdb_start_top_level_commissioning(mode);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal)
{
    const esp_zb_app_signal_type_t signal_type =
        (esp_zb_app_signal_type_t)*signal->p_app_signal;

    switch (signal_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION));
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up successfully");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
            } else {
                ESP_LOGI(TAG, "Device rebooted, already has network");
            }
        } else {
            ESP_LOGW(TAG, "Startup failed with status 0x%02x", signal->esp_err_status);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(TAG, "Joined network successfully! PAN ID: 0x%04hx, Channel: %d",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
        } else {
            ESP_LOGW(TAG, "Network steering failed with status 0x%02x, retrying in 1 s",
                     signal->esp_err_status);
            esp_zb_scheduler_alarm(zigbee_steering_retry, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGD(TAG, "Unhandled signal: %s (0x%02lx)",
                 esp_zb_zdo_signal_to_string(signal_type), (unsigned long)signal_type);
        break;
    }
}

static void pm_diagnostics_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "PM diagnostics while Zigbee is running");
        esp_pm_dump_locks(stdout);
    }
}

static void esp_zigbee_stack_main_task(void *arg)
{
    (void)arg;
    esp_zb_cfg_t config = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };

    esp_zb_init(&config);
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(esp_zb_set_secondary_network_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    esp_zb_set_rx_on_when_idle(false);
    ESP_ERROR_CHECK(zigbee_analog_devices_register());
    esp_zb_core_action_handler_register(zigbee_zcl_core_action_handler);
    ESP_ERROR_CHECK(esp_zb_start(false));
    xTaskCreate(pm_diagnostics_task, "PM_diagnostics", 6144, NULL, 1, NULL);

    while (true) {
        esp_zb_main_loop_iteration();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== APP START ===");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_pm_light_sleep_config());
    ESP_LOGI(TAG, "Start ESP Zigbee Stack");
    xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
