#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_1IN9_IMAGE_SIZE 15

extern unsigned char DSPNUM_9in1_off[EPD_1IN9_IMAGE_SIZE];

void epd_1in9_i2c_init(int sda_gpio, int scl_gpio);
void epd_1in9_gpio_init(gpio_num_t busy_pin, gpio_num_t rst_pin);
void epd_1in9_reset(void);
void epd_1in9_lut_du_wb(void);
void epd_1in9_lut_gc(void);
void epd_1in9_lut_5s(void);
void epd_1in9_init(void);
void epd_1in9_write_screen(const unsigned char *image, bool inverted_colors);
void epd_1in9_sleep(void);

#ifdef __cplusplus
}
#endif
