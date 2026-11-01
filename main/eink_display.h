#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

void eink_display_init(int sda_gpio, int scl_gpio, gpio_num_t busy_gpio, gpio_num_t rst_gpio);
void eink_display_set_numbers(float temperature, float humidity);
void eink_display_set_low_power_indicator(bool is_low_power);

// Отрисовывает накопленные изменения (если были) и укладывает контроллер дисплея в сон.
void eink_display_refresh(void);

#ifdef __cplusplus
}
#endif
