#include <string.h>
#include "epd_1in9.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EPD_1IN9";
// Полное (GC) обновление экрана идёт заметно дольше частичного (DU_WB) —
// 2000мс раньше хватало для DU_WB, но могло быть мало для GC. Если BUSY не
// успевает подняться за это время, код продолжает и шлёт следующие команды
// (в т.ч. sleep/power-off) экрану, который ещё физически занят обновлением —
// это может сбивать контроллер в плохое состояние до следующего включения.
#define EPD_BUSY_TIMEOUT_MS 5000

// Портировано с Arduino-версии (MQTT_Weather_Reader/lib/EinkDisplay/src/EPD_1in9.cpp)
// на нативный ESP-IDF I2C-драйвер (Wire -> driver/i2c_master.h, новый API — у
// старого driver/i2c.h после deep sleep стабильно отваливалась шина). Командные
// последовательности и тайминги оставлены как в оригинале.

#define ADDR_COM 0x3C
#define ADDR_DATA 0x3D
#define I2C_PORT I2C_NUM_0
#define I2C_TIMEOUT_MS 1000

unsigned char DSPNUM_9in1_off[EPD_1IN9_IMAGE_SIZE] = {0};

static gpio_num_t s_busy_pin = GPIO_NUM_1;
static gpio_num_t s_rst_pin = GPIO_NUM_2;

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev_com = NULL;
static i2c_master_dev_handle_t s_dev_data = NULL;

void epd_1in9_i2c_init(int sda_gpio, int scl_gpio)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    // Новый драйвер сам делает восстановление шины при создании — в отличие
    // от старого driver/i2c.h, который после deep sleep стабильно требовал RST.
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus));

    i2c_device_config_t dev_com_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_COM,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_com_cfg, &s_dev_com));

    i2c_device_config_t dev_data_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_DATA,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_data_cfg, &s_dev_data));
}

void epd_1in9_gpio_init(gpio_num_t busy_pin, gpio_num_t rst_pin)
{
    s_busy_pin = busy_pin;
    s_rst_pin = rst_pin;

    gpio_config_t busy_conf = {
        .pin_bit_mask = 1ULL << busy_pin,
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_conf));

    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << rst_pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_conf));
}

static void i2c_write_bytes(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = (addr == ADDR_COM) ? s_dev_com : s_dev_data;
    esp_err_t err = i2c_master_transmit(dev, data, len, I2C_TIMEOUT_MS);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C write to 0x%02X (%u байт, первый=0x%02X) не удалась: %s",
                 addr, (unsigned)len, len > 0 ? data[0] : 0, esp_err_to_name(err));
    }
}

static inline void send_command(uint8_t reg)
{
    i2c_write_bytes(ADDR_COM, &reg, 1);
}

void epd_1in9_reset(void)
{
    gpio_set_level(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void read_busy(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)EPD_BUSY_TIMEOUT_MS * 1000;
    while (gpio_get_level(s_busy_pin) != 1) {
        if (esp_timer_get_time() >= deadline_us) {
            ESP_LOGW(TAG, "BUSY не поднялся за %d мс (экран не подключён или неисправен) — продолжаем",
                     EPD_BUSY_TIMEOUT_MS);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void epd_1in9_lut_du_wb(void)
{
    uint8_t seq[] = {0x82, 0x80, 0x00, 0xC0, 0x80, 0x80, 0x62};
    i2c_write_bytes(ADDR_COM, seq, sizeof(seq));
}

void epd_1in9_lut_gc(void)
{
    uint8_t seq[] = {0x82, 0x20, 0x00, 0xA0, 0x80, 0x40, 0x63};
    i2c_write_bytes(ADDR_COM, seq, sizeof(seq));
}

void epd_1in9_lut_5s(void)
{
    uint8_t seq[] = {0x82, 0x28, 0x20, 0xA8, 0xA0, 0x50, 0x65};
    i2c_write_bytes(ADDR_COM, seq, sizeof(seq));
}

static void set_temperature_and_frame_time(void)
{
    // VAR_Temperature в оригинале фиксирован на 20°C (внешний датчик не используется)
    uint8_t seq1[] = {0x7E, 0x81, 0xB4};
    i2c_write_bytes(ADDR_COM, seq1, sizeof(seq1));
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t seq2[] = {0xE7, 0x0E}; // (14+1)*20ms = 300ms frame time при 15..20°C
    i2c_write_bytes(ADDR_COM, seq2, sizeof(seq2));
}

void epd_1in9_init(void)
{
    epd_1in9_reset();
    vTaskDelay(pdMS_TO_TICKS(100));

    send_command(0x2B); // POWER_ON
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t boost[] = {0xA7, 0xE0}; // boost, TSON
    i2c_write_bytes(ADDR_COM, boost, sizeof(boost));
    vTaskDelay(pdMS_TO_TICKS(10));

    set_temperature_and_frame_time();
}

void epd_1in9_write_screen(const unsigned char *image, bool inverted_colors)
{
    uint8_t seq1[] = {0xAC, 0x2B, 0x40, 0xA9, 0xA8};
    i2c_write_bytes(ADDR_COM, seq1, sizeof(seq1));

    uint8_t buf[EPD_1IN9_IMAGE_SIZE + 1];
    memcpy(buf, image, EPD_1IN9_IMAGE_SIZE);
    buf[EPD_1IN9_IMAGE_SIZE] = inverted_colors ? 0x03 : 0x00;
    i2c_write_bytes(ADDR_DATA, buf, sizeof(buf));

    uint8_t seq2[] = {0xAB, 0xAA, 0xAF};
    i2c_write_bytes(ADDR_COM, seq2, sizeof(seq2));

    read_busy();

    uint8_t seq3[] = {0xAE, 0x28, 0xAD};
    i2c_write_bytes(ADDR_COM, seq3, sizeof(seq3));
}

void epd_1in9_sleep(void)
{
    // Вариант с одним 0x28 не дал разницы (проверено) — возвращаем 0x28+0xAC
    // как в оригинале, раз содержимое команды сна оказалось ни при чём.
    read_busy();
    uint8_t seq[] = {0x28, 0xAC}; // POWER_OFF, DEEP_SLEEP
    i2c_write_bytes(ADDR_COM, seq, sizeof(seq));
}
