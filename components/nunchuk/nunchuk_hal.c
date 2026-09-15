#include "nunchuk.h"
#include "config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Используется "классический" driver/i2c.h (не новый i2c_master.h из
 * ESP-IDF 5.2+) — сохраняет совместимость с более широким диапазоном
 * версий IDF, которые может подтянуть PlatformIO. При необходимости
 * замена на новый API локализована в этом файле. */

#define NUNCHUK_I2C_ADDR   0x52
#define I2C_TIMEOUT_MS     50

static const char *TAG = "nunchuk";
static uint32_t s_consecutive_failures = 0;

static void busy_wait_us(uint32_t us)
{
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < (int64_t)us) {
        /* короткая занятая пауза для процедуры bus recovery (десятки мкс) */
    }
}

static esp_err_t i2c_write_bytes(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NUNCHUK_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(MODULE_I2C_PORT_NUM, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_read_bytes(uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NUNCHUK_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(MODULE_I2C_PORT_NUM, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t do_handshake(void)
{
    uint8_t init1[2] = { 0xF0, 0x55 };
    esp_err_t err = i2c_write_bytes(init1, sizeof(init1));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t init2[2] = { 0xFB, 0x00 };
    err = i2c_write_bytes(init2, sizeof(init2));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

static esp_err_t install_i2c_driver(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MODULE_I2C_SDA_GPIO,
        .scl_io_num = MODULE_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MODULE_I2C_CLOCK_HZ,
    };
    esp_err_t err = i2c_param_config(MODULE_I2C_PORT_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(MODULE_I2C_PORT_NUM, conf.mode, 0, 0, 0);
}

/* FR-37.2: восстановление зависшей шины I2C — классическая процедура
 * "дотолкать" ведомого, удерживающего SDA в 0, вручную сформированными
 * тактами SCL, затем сформировать STOP и переинициализировать драйвер. */
static void i2c_bus_recovery(void)
{
    ESP_LOGW(TAG, "I2C bus recovery (SDA/SCL GPIO %d/%d)",
             MODULE_I2C_SDA_GPIO, MODULE_I2C_SCL_GPIO);

    i2c_driver_delete(MODULE_I2C_PORT_NUM);

    gpio_config_t scl_conf = {
        .pin_bit_mask = (1ULL << MODULE_I2C_SCL_GPIO),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&scl_conf);
    gpio_config_t sda_in_conf = {
        .pin_bit_mask = (1ULL << MODULE_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sda_in_conf);

    gpio_set_level(MODULE_I2C_SCL_GPIO, 1);
    for (int i = 0; i < 9; i++) {
        if (gpio_get_level(MODULE_I2C_SDA_GPIO)) {
            break; /* ведомый уже отпустил SDA */
        }
        gpio_set_level(MODULE_I2C_SCL_GPIO, 0);
        busy_wait_us(5);
        gpio_set_level(MODULE_I2C_SCL_GPIO, 1);
        busy_wait_us(5);
    }

    /* STOP: SDA 0->1 при SCL=1 */
    gpio_config_t sda_out_conf = {
        .pin_bit_mask = (1ULL << MODULE_I2C_SDA_GPIO),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sda_out_conf);
    gpio_set_level(MODULE_I2C_SDA_GPIO, 0);
    busy_wait_us(5);
    gpio_set_level(MODULE_I2C_SCL_GPIO, 1);
    busy_wait_us(5);
    gpio_set_level(MODULE_I2C_SDA_GPIO, 1);
    busy_wait_us(5);

    install_i2c_driver();
    do_handshake();
}

esp_err_t nunchuk_hal_init(void)
{
    esp_err_t err = install_i2c_driver();
    if (err != ESP_OK) {
        return err;
    }
    s_consecutive_failures = 0;
    return do_handshake();
}

esp_err_t nunchuk_hal_read(nunchuk_sample_t *out)
{
    uint8_t request = 0x00;
    esp_err_t err = i2c_write_bytes(&request, 1);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
        uint8_t raw[6];
        err = i2c_read_bytes(raw, sizeof(raw));
        if (err == ESP_OK) {
            nunchuk_decode(raw, out);
        }
    }

    if (err == ESP_OK) {
        s_consecutive_failures = 0;
    } else {
        s_consecutive_failures++;
        ESP_LOGW(TAG, "read failed (%s), consecutive=%u",
                 esp_err_to_name(err), (unsigned)s_consecutive_failures);
        if (s_consecutive_failures >= MODULE_I2C_FAIL_THRESHOLD &&
            (s_consecutive_failures % MODULE_I2C_FAIL_THRESHOLD) == 0) {
            i2c_bus_recovery();
        }
    }
    return err;
}

uint32_t nunchuk_hal_consecutive_failures(void)
{
    return s_consecutive_failures;
}
