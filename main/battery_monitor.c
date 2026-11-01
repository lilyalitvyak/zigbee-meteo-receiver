#include "battery_monitor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

// GPIO0 = A0 на XIAO ESP32C6, ADC1 channel 0.
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_DIVIDER_RATIO 2.0f // делитель 220к/220к -> напряжение на A0 вдвое меньше BAT+
#define BATTERY_SAMPLE_COUNT 16

static const char *TAG = "BATTERY";
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_ok = false;

esp_err_t battery_monitor_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Не удалось создать ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Не удалось настроить ADC канал: %s", esp_err_to_name(err));
        return err;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK);
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "Калибровка ADC недоступна на этом чипе, используем приблизительный расчёт");
    }
    return ESP_OK;
}

float battery_monitor_read_voltage(void)
{
    int64_t sum_mv = 0;
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        int mv;
        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        } else {
            mv = (int)((raw / 4095.0f) * 3300.0f);
        }
        sum_mv += mv;
    }
    float avg_mv = (float)sum_mv / BATTERY_SAMPLE_COUNT;
    return BATTERY_DIVIDER_RATIO * avg_mv / 1000.0f;
}
