#include "buzzer.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static buzzer_mode_t s_mode = BUZZER_MODE_OFF;

void buzzer_hal_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODULE_BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MODULE_BUZZER_GPIO, 0);
}

void buzzer_hal_set_mode(buzzer_mode_t mode)
{
    s_mode = mode;
    if (mode == BUZZER_MODE_OFF) {
        gpio_set_level(MODULE_BUZZER_GPIO, 0); /* не ждать следующего tick() для тишины */
    }
}

void buzzer_hal_tick(void)
{
    uint32_t freq_hz = 0;
    switch (s_mode) {
    case BUZZER_MODE_BEEP_HOLD: freq_hz = MODULE_BUZZER_BEEP_HZ_HOLD; break;
    case BUZZER_MODE_BEEP_COUNTDOWN: freq_hz = MODULE_BUZZER_BEEP_HZ_COUNTDOWN; break;
    case BUZZER_MODE_OFF: default: return; /* GPIO уже в 0, см. buzzer_hal_set_mode() */
    }
    bool on = buzzer_square_on(esp_timer_get_time() / 1000, freq_hz);
    gpio_set_level(MODULE_BUZZER_GPIO, on ? 1 : 0);
}
