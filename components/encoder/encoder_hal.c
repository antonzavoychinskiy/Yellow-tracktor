#include "encoder.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/*
 * NOTE: encoder_hal_button_update() и encoder_hal_take_delta() должны
 * опрашиваться заметно чаще, чем общий период отправки MANUAL_CONTROL
 * (MODULE_MANUAL_CONTROL_PERIOD_MS) — иначе антидребезг и классификация
 * короткого/длинного нажатия будут грубыми. Ориентир: не реже 100 Гц.
 * Точное место в расписании задач определяется на этапе 4.
 */

static encoder_quad_t s_quad;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t s_delta_accum = 0;
static volatile int64_t s_suppress_until_us = 0;

static encoder_button_t s_button;
static bool s_isr_service_installed = false;

static bool raw_sw_pressed(void)
{
    int level = gpio_get_level(MODULE_ENCODER_SW_GPIO);
    return MODULE_ENCODER_SW_ACTIVE_LOW ? (level == 0) : (level == 1);
}

static void IRAM_ATTR quad_isr_handler(void *arg)
{
    bool a = gpio_get_level(MODULE_ENCODER_CLK_GPIO) == 1;
    bool b = gpio_get_level(MODULE_ENCODER_DT_GPIO) == 1;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&s_mux);
    encoder_dir_t dir = encoder_quad_step(&s_quad, a, b);
    if (dir != ENCODER_DIR_NONE && now >= s_suppress_until_us) {
        s_delta_accum += (int32_t)dir;
    }
    portEXIT_CRITICAL_ISR(&s_mux);
}

void encoder_hal_init(void)
{
    gpio_config_t rot_conf = {
        .pin_bit_mask = (1ULL << MODULE_ENCODER_CLK_GPIO) | (1ULL << MODULE_ENCODER_DT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&rot_conf);

    gpio_config_t sw_conf = {
        .pin_bit_mask = (1ULL << MODULE_ENCODER_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = MODULE_ENCODER_SW_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = MODULE_ENCODER_SW_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_conf);

    encoder_quad_init(&s_quad);

    if (!s_isr_service_installed) {
        gpio_install_isr_service(0);
        s_isr_service_installed = true;
    }
    gpio_isr_handler_add(MODULE_ENCODER_CLK_GPIO, quad_isr_handler, NULL);
    gpio_isr_handler_add(MODULE_ENCODER_DT_GPIO, quad_isr_handler, NULL);

    encoder_button_init(&s_button, raw_sw_pressed(), MODULE_DEBOUNCE_MS * 1000u);
}

int32_t encoder_hal_take_delta(void)
{
    int32_t value;
    portENTER_CRITICAL(&s_mux);
    value = s_delta_accum;
    s_delta_accum = 0;
    portEXIT_CRITICAL(&s_mux);
    return value;
}

encoder_btn_event_t encoder_hal_button_update(void)
{
    int64_t now = esp_timer_get_time();
    bool prev_level = debounce_level(&s_button.db);

    encoder_btn_event_t event = encoder_button_update(&s_button, raw_sw_pressed(), now,
                                                        MODULE_ENCODER_LONGPRESS_MS * 1000u);

    bool new_level = debounce_level(&s_button.db);
    if (new_level != prev_level) {
        /* FR-39.1: подавляем вращение в защитном окне вокруг момента
         * нажатия/отпускания кнопки энкодера. */
        portENTER_CRITICAL(&s_mux);
        s_suppress_until_us = now + (int64_t)MODULE_ENCODER_PRESS_GUARD_MS * 1000;
        s_delta_accum = 0;
        portEXIT_CRITICAL(&s_mux);
    }

    return event;
}

uint32_t encoder_hal_hold_progress_permille(void)
{
    return encoder_button_hold_progress_permille(&s_button, esp_timer_get_time(),
                                                   MODULE_ENCODER_LONGPRESS_MS * 1000u);
}
