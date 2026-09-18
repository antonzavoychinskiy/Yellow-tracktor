#include "buttons.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static button_t s_start_btn;
static button_t s_back_btn;
static button_t s_dead_man_btn;

static bool raw_level_pressed(int gpio, int active_low)
{
    int level = gpio_get_level(gpio);
    return active_low ? (level == 0) : (level == 1);
}

static bool raw_pressed(int gpio)
{
    return raw_level_pressed(gpio, MODULE_BUTTON_ACTIVE_LOW);
}

static bool raw_dead_man_pressed(void)
{
    return raw_level_pressed(MODULE_DEAD_MAN_GPIO, MODULE_DEAD_MAN_ACTIVE_LOW);
}

static void configure_pin_with_polarity(int gpio, int active_low)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

static void configure_pin(int gpio)
{
    configure_pin_with_polarity(gpio, MODULE_BUTTON_ACTIVE_LOW);
}

void buttons_hal_init(void)
{
    configure_pin(MODULE_BUTTON_START_GPIO);
    configure_pin(MODULE_BUTTON_BACK_GPIO);
    configure_pin_with_polarity(MODULE_DEAD_MAN_GPIO, MODULE_DEAD_MAN_ACTIVE_LOW);

    button_init(&s_start_btn, raw_pressed(MODULE_BUTTON_START_GPIO), MODULE_DEBOUNCE_MS * 1000u);
    button_init(&s_back_btn, raw_pressed(MODULE_BUTTON_BACK_GPIO), MODULE_DEBOUNCE_MS * 1000u);
    button_init(&s_dead_man_btn, raw_dead_man_pressed(), MODULE_DEBOUNCE_MS * 1000u);
}

bool buttons_hal_start_pressed(void)
{
    return button_update(&s_start_btn, raw_pressed(MODULE_BUTTON_START_GPIO), esp_timer_get_time());
}

bool buttons_hal_back_pressed(void)
{
    return button_update(&s_back_btn, raw_pressed(MODULE_BUTTON_BACK_GPIO), esp_timer_get_time());
}

bool buttons_hal_start_held(void)
{
    return debounce_level(&s_start_btn.db);
}

bool buttons_hal_dead_man_held(void)
{
    bool raw = raw_dead_man_pressed();
    button_update(&s_dead_man_btn, raw, esp_timer_get_time());

    /* Нажатие — только после устойчивого уровня (FR-39), отпускание —
     * сразу по сырому уровню, без задержки антидребезга: раннее снятие
     * разрешения безопасно, а задержка нарушила бы NFR-6 (нейтраль не
     * позднее одного цикла отправки). */
    return raw && debounce_level(&s_dead_man_btn.db);
}
