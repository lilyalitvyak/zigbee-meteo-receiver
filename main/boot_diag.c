#include <stdbool.h>
#include "boot_diag.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "BOOT_DIAG";

#define BOOT_DIAG_HISTORY_LEN 12
#define BOOT_DIAG_EMPTY_SLOT 0xFFU

// checkpoint/reset_reason для последних BOOT_DIAG_HISTORY_LEN завершившихся
// циклов (каждый цикл — это deep sleep wake, который снова окончился
// сбросом). RTC_DATA_ATTR переживает deep sleep, но не сброс по питанию
// (holod boot стирает историю — это нормально, интересуют именно повторные
// deep-sleep циклы).
static RTC_DATA_ATTR uint8_t s_history_checkpoint[BOOT_DIAG_HISTORY_LEN];
static RTC_DATA_ATTR uint8_t s_history_reset_reason[BOOT_DIAG_HISTORY_LEN];
static RTC_DATA_ATTR uint8_t s_history_index;
static RTC_DATA_ATTR bool s_history_valid;
static RTC_DATA_ATTR uint8_t s_current_checkpoint;

static const char *checkpoint_name(uint8_t cp)
{
    switch ((boot_checkpoint_t)cp) {
    case BOOT_CP_START: return "START";
    case BOOT_CP_STARTUP_SLEEP_DONE: return "STARTUP_SLEEP_DONE";
    case BOOT_CP_DISPLAY_INIT_DONE: return "DISPLAY_INIT_DONE";
    case BOOT_CP_NVS_INIT_DONE: return "NVS_INIT_DONE";
    case BOOT_CP_ZB_TASK_STARTED: return "ZB_TASK_STARTED";
    case BOOT_CP_ZB_STACK_STARTED: return "ZB_STACK_STARTED";
    case BOOT_CP_ZB_REJOINED: return "ZB_REJOINED";
    case BOOT_CP_ZB_STEERING_OK: return "ZB_STEERING_OK";
    case BOOT_CP_AO_WAIT_STARTED: return "AO_WAIT_STARTED";
    case BOOT_CP_AO_TIMEOUT: return "AO_TIMEOUT";
    case BOOT_CP_DISPLAY_REFRESHED: return "DISPLAY_REFRESHED";
    case BOOT_CP_ENTERING_SLEEP: return "ENTERING_SLEEP";
    default: return cp == BOOT_DIAG_EMPTY_SLOT ? "-" : "UNKNOWN";
    }
}

static const char *reset_reason_name(uint8_t reason)
{
    switch ((esp_reset_reason_t)reason) {
    case ESP_RST_UNKNOWN: return "UNKNOWN";
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return reason == BOOT_DIAG_EMPTY_SLOT ? "-" : "UNKNOWN";
    }
}

void boot_diag_init_and_dump(void)
{
    esp_reset_reason_t reset_reason = esp_reset_reason();

    if (!s_history_valid) {
        s_history_valid = true;
        s_history_index = 0;
        for (int i = 0; i < BOOT_DIAG_HISTORY_LEN; i++) {
            s_history_checkpoint[i] = BOOT_DIAG_EMPTY_SLOT;
            s_history_reset_reason[i] = BOOT_DIAG_EMPTY_SLOT;
        }
    } else {
        // Чекпоинт, на котором остановился ПРЕДЫДУЩИЙ цикл, вместе с
        // причиной сброса, который его прервал (или которым он "нормально"
        // завершился, если это ENTERING_SLEEP -> DEEPSLEEP).
        s_history_checkpoint[s_history_index] = s_current_checkpoint;
        s_history_reset_reason[s_history_index] = (uint8_t)reset_reason;
        s_history_index = (uint8_t)((s_history_index + 1) % BOOT_DIAG_HISTORY_LEN);
    }

    ESP_LOGI(TAG, "=== История последних циклов (RTC-память, переживает deep sleep) ===");
    bool any = false;
    for (int i = 0; i < BOOT_DIAG_HISTORY_LEN; i++) {
        int idx = (s_history_index + i) % BOOT_DIAG_HISTORY_LEN;
        if (s_history_checkpoint[idx] == BOOT_DIAG_EMPTY_SLOT) {
            continue;
        }
        any = true;
        ESP_LOGI(TAG, "  цикл %d: последний чекпоинт=%s, сброс из-за=%s",
                 i, checkpoint_name(s_history_checkpoint[idx]), reset_reason_name(s_history_reset_reason[idx]));
    }
    if (!any) {
        ESP_LOGI(TAG, "  (пусто — первая загрузка после холодного старта/потери питания)");
    }
    ESP_LOGI(TAG, "=== Текущая загрузка: reset_reason=%s ===", reset_reason_name((uint8_t)reset_reason));

    s_current_checkpoint = BOOT_CP_START;
}

void boot_diag_checkpoint(boot_checkpoint_t cp)
{
    s_current_checkpoint = (uint8_t)cp;
    ESP_LOGI(TAG, "checkpoint: %s", checkpoint_name((uint8_t)cp));
}
