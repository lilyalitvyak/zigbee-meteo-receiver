#include <string.h>
#include <math.h>
#include "eink_display.h"
#include "epd_1in9.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Портировано с Arduino-версии (MQTT_Weather_Reader/lib/EinkDisplay) один в один:
// таблица сегментов, разбор чисел на цифры и маски битов те же самые.

#define CELSIUS 0x05
#define DOT 0b0000000000100000
#define PERCENT 0b0000000000100000
#define LOW_POWER_ON 0b0000000000010000
#define LOW_POWER_OFF 0b1111111111101111

static const uint8_t DIGITS[10][2] = {
    {0xbf, 0xff}, // 0
    {0x00, 0xff}, // 1
    {0xfd, 0x17}, // 2
    {0xf5, 0xff}, // 3
    {0x47, 0xff}, // 4
    {0xf7, 0x1d}, // 5
    {0xff, 0x1d}, // 6
    {0x21, 0xff}, // 7
    {0xff, 0xff}, // 8
    {0xf7, 0xff}, // 9
};
static const uint8_t MINUS_SIGN[2] = {0b01000100, 0b00000000};
#define EMPTY_PIXEL 0x00

static bool s_need_update = false;
static uint8_t s_degrees_type = CELSIUS;
static uint8_t s_image[EPD_1IN9_IMAGE_SIZE] = {0};
static int s_temperature_digits[4] = {-1, -1, -1, -1};
static int s_humidity_digits[3] = {-1, -1, -1};

static inline uint8_t get_pixel(int number, int order)
{
    return number == -1 ? EMPTY_PIXEL : DIGITS[number][order];
}

static void parse_number(float number, int *digits, int digits_count)
{
    if (isnan(number)) {
        for (int i = 0; i < digits_count; i++) {
            digits[i] = -1;
        }
        return;
    }

    // Округляем к ближайшей десятой ЦЕЛЫМ числом и извлекаем цифры из него,
    // а не делением float на степени десяти — иначе дробная часть отбрасывается
    // усечением (24.76 -> "24.7"), а не округляется до ближайшего значения (24.8).
    long total_tenths = lroundf(number * 10.0f);
    for (int i = 0; i < digits_count; i++) {
        long divisor = 1;
        for (int p = 0; p < digits_count - i - 1; p++) {
            divisor *= 10;
        }
        digits[i] = (int)((total_tenths / divisor) % 10);
        if (digits[i] == 0 && (i == 0 || digits[i - 1] == -1)) {
            digits[i] = -1;
        }
    }
}

static void set_data(const uint8_t new_image[EPD_1IN9_IMAGE_SIZE])
{
    for (int i = 0; i < EPD_1IN9_IMAGE_SIZE; i++) {
        if (new_image[i] != s_image[i]) {
            s_image[i] = new_image[i];
            s_need_update = true;
        }
    }
}

// Последнее изображение, реально выведенное на экран. RTC-память переживает
// deep sleep (но обнуляется при подаче питания), поэтому по ней можно понять,
// изменились ли цифры, и не трогать экран, если нет.
static RTC_DATA_ATTR uint8_t s_rtc_image[EPD_1IN9_IMAGE_SIZE];
static RTC_DATA_ATTR bool s_rtc_valid = false;

static int s_sda_gpio, s_scl_gpio;
static gpio_num_t s_busy_gpio, s_rst_gpio;
static bool s_cold_start = true;

void eink_display_init(int sda_gpio, int scl_gpio, gpio_num_t busy_gpio, gpio_num_t rst_gpio)
{
    s_sda_gpio = sda_gpio;
    s_scl_gpio = scl_gpio;
    s_busy_gpio = busy_gpio;
    s_rst_gpio = rst_gpio;

    memset(s_image, 0, sizeof(s_image));
    s_degrees_type = CELSIUS;
    s_image[13] = s_degrees_type;

    s_cold_start = !(esp_reset_reason() == ESP_RST_DEEPSLEEP && s_rtc_valid);
    if (!s_cold_start) {
        // Пробуждение из нашего deep sleep: восстанавливаем то, что уже на
        // экране, чтобы set_data() обновлял экран только при реальном изменении.
        memcpy(s_image, s_rtc_image, sizeof(s_image));
        s_degrees_type = s_image[13];
    }
    // Само оборудование здесь НЕ трогаем: экран инициализируется лениво в
    // eink_display_refresh(), и только если есть что обновлять.
}

void eink_display_set_numbers(float temperature, float humidity)
{
    bool temp_positive = temperature > 0;
    bool humid_positive = humidity > 0;
    parse_number(temp_positive ? temperature : -temperature, s_temperature_digits, 4);
    parse_number(humid_positive ? humidity : -humidity, s_humidity_digits, 3);

    uint8_t new_image[EPD_1IN9_IMAGE_SIZE] = {
        get_pixel(s_temperature_digits[0], 1),
        temp_positive ? get_pixel(s_temperature_digits[1], 0) : MINUS_SIGN[0],
        temp_positive ? get_pixel(s_temperature_digits[1], 1) : MINUS_SIGN[1],
        get_pixel(s_temperature_digits[2], 0),
        get_pixel(s_temperature_digits[2], 1),
        humid_positive ? get_pixel(s_humidity_digits[0], 0) : MINUS_SIGN[0],
        humid_positive ? get_pixel(s_humidity_digits[0], 1) : MINUS_SIGN[1],
        get_pixel(s_humidity_digits[1], 0),
        get_pixel(s_humidity_digits[1], 1),
        get_pixel(s_humidity_digits[2], 0),
        get_pixel(s_humidity_digits[2], 1),
        get_pixel(s_temperature_digits[3], 0),
        get_pixel(s_temperature_digits[3], 1),
        s_degrees_type, // C°/F°/индикатор батареи/bluetooth
        s_image[14],
    };

    new_image[4] |= DOT;      // верхняя точка (температура)
    new_image[8] |= DOT;      // нижняя точка (влажность)
    new_image[10] |= PERCENT; // символ процента

    set_data(new_image);
}

void eink_display_set_low_power_indicator(bool is_low_power)
{
    if (is_low_power) {
        s_degrees_type |= LOW_POWER_ON;
    } else {
        s_degrees_type &= LOW_POWER_OFF;
    }
    // degreesType хранится и в 13-м байте изображения — обновим его напрямую,
    // set_numbers() в этом же цикле всё равно перезапишет байт актуальным значением.
    if (s_image[13] != s_degrees_type) {
        s_image[13] = s_degrees_type;
        s_need_update = true;
    }
}

void eink_display_refresh(void)
{
    if (!s_need_update) {
        return; // цифры не изменились — экран не трогаем вообще
    }

    epd_1in9_i2c_init(s_sda_gpio, s_scl_gpio);
    epd_1in9_gpio_init(s_busy_gpio, s_rst_gpio);

    // RST + POWER_ON/boost/Temperature нужны на КАЖДОМ обновлении: без RST
    // I2C-шина ESP-IDF после deep sleep стабильно ломается (проверено на
    // железе, похоже на esp-idf#18105).
    epd_1in9_init();

    if (s_cold_start) {
        // Первое включение питания: производитель рекомендует сначала
        // полностью очистить экран.
        epd_1in9_lut_5s();
        epd_1in9_write_screen(DSPNUM_9in1_off, false);
    }

    // После RST внутренний буфер предыдущего кадра контроллера сброшен, поэтому
    // DU_WB (частичное обновление) не гасит старые сегменты — используем
    // полное обновление GC (мигает, зато чисто).
    epd_1in9_lut_gc();
    vTaskDelay(pdMS_TO_TICKS(300));
    epd_1in9_write_screen(s_image, false);
    epd_1in9_sleep();

    memcpy(s_rtc_image, s_image, sizeof(s_rtc_image));
    s_rtc_valid = true;
    s_cold_start = false;
    s_need_update = false;
}
