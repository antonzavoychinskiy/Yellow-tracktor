#include "buttons.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static button_t s_start_btn;
static button_t s_back_btn;

static bool raw_pressed(int gpio)
{
    int level = gpio_get_level(gpio);
    return MODULE_BUTTON_ACTIVE_LOW ? (level == 0) : (level == 1);
}

static void configure_pin(int gpio)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = MODULE_BUTTON_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = MODULE_BUTTON_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void buttons_hal_init(void)
{
    configure_pin(MODULE_BUTTON_START_GPIO);
    configure_pin(MODULE_BUTTON_BACK_GPIO);

    button_init(&s_start_btn, raw_pressed(MODULE_BUTTON_START_GPIO), MODULE_DEBOUNCE_MS * 1000u);
    button_init(&s_back_btn, raw_pressed(MODULE_BUTTON_BACK_GPIO), MODULE_DEBOUNCE_MS * 1000u);
}

bool buttons_hal_start_pressed(void)
{
    return button_update(&s_start_btn, raw_pressed(MODULE_BUTTON_START_GPIO), esp_timer_get_time());
}

bool buttons_hal_back_pressed(void)
{
    return button_update(&s_back_btn, raw_pressed(MODULE_BUTTON_BACK_GPIO), esp_timer_get_time());
}
