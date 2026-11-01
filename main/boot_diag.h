#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Лёгкая диагностика для случаев, когда живой serial-лог недоступен
// (например, устройство работает только от батареи). Чекпоинты хранятся в
// RTC-памяти, которая переживает deep sleep, поэтому история последних
// циклов видна сразу при следующей загрузке — даже если именно тот цикл,
// где что-то пошло не так, никто не наблюдал вживую.
typedef enum {
    BOOT_CP_START = 0,
    BOOT_CP_STARTUP_SLEEP_DONE,
    BOOT_CP_DISPLAY_INIT_DONE,
    BOOT_CP_NVS_INIT_DONE,
    BOOT_CP_ZB_TASK_STARTED,
    BOOT_CP_ZB_STACK_STARTED,
    BOOT_CP_ZB_REJOINED,
    BOOT_CP_ZB_STEERING_OK,
    BOOT_CP_AO_WAIT_STARTED,
    BOOT_CP_AO_TIMEOUT,
    BOOT_CP_DISPLAY_REFRESHED,
    BOOT_CP_ENTERING_SLEEP,
} boot_checkpoint_t;

// Вызывать в самом начале app_main(), до любой другой инициализации.
// Печатает накопленную историю предыдущих циклов и причину текущего сброса.
void boot_diag_init_and_dump(void);

// Отмечает текущий чекпоинт: сразу пишет в лог (если он виден) и сохраняет
// в RTC-память на случай, если лог сейчас не виден.
void boot_diag_checkpoint(boot_checkpoint_t cp);

#ifdef __cplusplus
}
#endif
