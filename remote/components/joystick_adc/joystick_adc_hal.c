#include "joystick_adc.h"
#include "config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

/* Аттенюация 12 дБ — полный диапазон входа ~0..3.1 В, соответствует
 * измеренному размаху сигнала при питании джойстика 3.3 В (открытый
 * вопрос №8). Разрядность 12 бит: сырые отсчёты 0..4095, на эту шкалу
 * рассчитаны MODULE_JOYSTICK_ADC_CENTER_* и RAW_FULL_SCALE. */
#define JOYSTICK_ADC_ATTEN      ADC_ATTEN_DB_12
#define JOYSTICK_ADC_BITWIDTH   ADC_BITWIDTH_12

static const char *TAG = "joystick";

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_chan_x;
static adc_channel_t s_chan_y;
static uint32_t s_consecutive_failures;

static esp_err_t setup_channel(int gpio, adc_channel_t *out_chan)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, out_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d is not an ADC pad (%s)", gpio, esp_err_to_name(err));
        return err;
    }
    if (unit != ADC_UNIT_1) {
        /* ADC2 на ESP32 недоступен, пока работает Wi-Fi, и конфликтует
         * по таймингам — оси обязаны быть на ADC1 (п. 3.2: GPIO36/39). */
        ESP_LOGE(TAG, "GPIO %d is on ADC2, only ADC1 is usable", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_chan_cfg_t cfg = {
        .atten = JOYSTICK_ADC_ATTEN,
        .bitwidth = JOYSTICK_ADC_BITWIDTH,
    };
    return adc_oneshot_config_channel(s_adc, *out_chan, &cfg);
}

esp_err_t joystick_adc_hal_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        return err;
    }

    err = setup_channel(MODULE_JOYSTICK_X_GPIO, &s_chan_x);
    if (err != ESP_OK) {
        return err;
    }
    err = setup_channel(MODULE_JOYSTICK_Y_GPIO, &s_chan_y);
    if (err != ESP_OK) {
        return err;
    }

    s_consecutive_failures = 0;
    return ESP_OK;
}

esp_err_t joystick_adc_hal_read(joystick_adc_sample_t *out)
{
    int raw_x = 0;
    int raw_y = 0;

    esp_err_t err = adc_oneshot_read(s_adc, s_chan_x, &raw_x);
    if (err == ESP_OK) {
        err = adc_oneshot_read(s_adc, s_chan_y, &raw_y);
    }

    if (err != ESP_OK) {
        s_consecutive_failures++;
        ESP_LOGW(TAG, "read failed (%s), consecutive=%u",
                 esp_err_to_name(err), (unsigned)s_consecutive_failures);
        return err;
    }

    s_consecutive_failures = 0;
    joystick_adc_center(raw_x, raw_y, out);
    return ESP_OK;
}

uint32_t joystick_adc_hal_consecutive_failures(void)
{
    return s_consecutive_failures;
}
