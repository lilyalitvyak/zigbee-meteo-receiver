#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_zigbee_core.h"
#include "main.h"
#include "zigbee_device.h"

static const char *TAG = "MAIN";
static const char *SIGNAL_HANDLER_TAG = "SIGNAL_HANDLER";

#define STARTUP_LIGHT_SLEEP_MS 200U
#define STARTUP_DEBUG_DELAY_MS 3000U
#define DEEP_SLEEP_SECONDS 110U

typedef enum {
    APP_STATE_WAIT_AO_VALUES = 0,
    APP_STATE_ENTER_DEEP_SLEEP,
} app_cycle_state_t;

static app_cycle_state_t s_cycle_state = APP_STATE_WAIT_AO_VALUES;

static void startup_light_sleep_once(void)
{
#if defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    ESP_LOGW(TAG, "USB-Serial/JTAG активен: стартовый light sleep пропущен для стабильного monitor");
    return;
#endif
    ESP_LOGI(TAG, "Запуск в light sleep на %u ms", STARTUP_LIGHT_SLEEP_MS);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)STARTUP_LIGHT_SLEEP_MS * 1000ULL));
    esp_light_sleep_start();
    ESP_LOGI(TAG, "Выход из стартового light sleep");
}

static void enter_deep_sleep_110s(void)
{
    const uint64_t sleep_us = (uint64_t)DEEP_SLEEP_SECONDS * 1000000ULL;
    ESP_LOGI(TAG, "Температура и влажность получены через Analog Output. Уход в deep sleep на %u секунд",
             DEEP_SLEEP_SECONDS);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_us));
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_deep_sleep_start();
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal)
{
    const esp_zb_app_signal_type_t signal_type =
        (esp_zb_app_signal_type_t)*signal->p_app_signal;

    switch (signal_type) {
    // Стек запущен в режиме no-autostart. Здесь запускаем начальную
    // инициализацию BDB.
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(SIGNAL_HANDLER_TAG, "Инициализация стека Zigbee");
        ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION));
        break;
    // Первый запуск после очистки Zigbee NVRAM.
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Устройство успешно запустилось");
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Запущен network steering, ожидаем подключение к сети");
        } else {
            ESP_LOGW(SIGNAL_HANDLER_TAG, "Запуск завершился с ошибкой: status=0x%02x", signal->esp_err_status);
        }
        break;
    // Повторный запуск с сохранёнными сетевыми параметрами.
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (signal->esp_err_status == ESP_OK && !esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Устройство перезагрузилось и уже имеет сеть");
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Ожидаем значения Analog Output температуры и влажности");
        } else {
            ESP_LOGW(SIGNAL_HANDLER_TAG, "Загрузка сохранённой сети не удалась или устройство фабрично новое: status=0x%02x. Запускаем steering заново.",
                     signal->esp_err_status);
            ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Успешное подключение к сети! PAN ID: 0x%04hx, Channel: %d",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            zigbee_remote_values_reset();
            ESP_LOGI(SIGNAL_HANDLER_TAG, "Ожидание новых значений Analog Output от z2m");
        } else {
            ESP_LOGW(SIGNAL_HANDLER_TAG, "Steering сети завершился с ошибкой: status=0x%02x",
                     signal->esp_err_status);
        }
        break;
    default:
        ESP_LOGD(SIGNAL_HANDLER_TAG, "Unhandled signal: %s (0x%02lx)",
                 esp_zb_zdo_signal_to_string(signal_type), (unsigned long)signal_type);
        break;
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
    esp_zb_zcl_command_send_status_handler_register(zigbee_command_send_status_cb);
    ESP_ERROR_CHECK(esp_zb_start(false));

    int64_t last_log_us = 0;

    while (true) {
        switch (s_cycle_state) {
        case APP_STATE_WAIT_AO_VALUES:
            if ((esp_timer_get_time() - last_log_us) >= 5000000LL) {
                last_log_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Ожидание Analog Output температуры и влажности от z2m... joined=%s ready=%s",
                         esp_zb_bdb_dev_joined() ? "true" : "false",
                         zigbee_remote_values_ready() ? "true" : "false");
            }

            if (zigbee_take_deep_sleep_request()) {
                ESP_LOGI(TAG, "Получен запрос на deep sleep от обработчика AO");
                s_cycle_state = APP_STATE_ENTER_DEEP_SLEEP;
            }
            break;

        case APP_STATE_ENTER_DEEP_SLEEP:
            enter_deep_sleep_110s();
            break;

        default:
            break;
        }

        esp_zb_main_loop_iteration();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ЗАПУСК ПРИЛОЖЕНИЯ ===");
    startup_light_sleep_once();
    ESP_LOGI(TAG, "Таймер 1 запущен: отладочная пауза %u ms", STARTUP_DEBUG_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DEBUG_DELAY_MS));
    ESP_LOGI(TAG, "Таймер 1 завершён: переходим к ожиданию данных Analog Output");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "Запуск стека ESP Zigbee");
    xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
