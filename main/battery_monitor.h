#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализирует ADC на GPIO0 (A0 на XIAO ESP32C6) для чтения напряжения батареи
// через внешний резистивный делитель 1:2 (220кОм + 220кОм, BAT+ -> A0 -> GND).
esp_err_t battery_monitor_init(void);

// Возвращает напряжение батареи в вольтах (уже с учётом делителя).
float battery_monitor_read_voltage(void);

#ifdef __cplusplus
}
#endif
