#include "keyswitch.h"
#include "config.h"
#include "debounce.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static debounce_t s_local_db;
static debounce_t s_auto_db;

static bool raw_contact(int gpio, bool active_high)
{
    int level = gpio_get_level(gpio);
    return active_high ? (level == 1) : (level == 0);
}

void keyswitch_hal_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODULE_KEY_LOCAL_GPIO) | (1ULL << MODULE_KEY_AUTO_GPIO),
        .mode = GPIO_MODE_INPUT,
        /* GPIO34/35 не поддерживают внутреннюю подтяжку — внешние
         * резисторы обязательны в схеме (см. config.h). */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool local0 = raw_contact(MODULE_KEY_LOCAL_GPIO, MODULE_KEY_LOCAL_ACTIVE_HIGH);
    bool auto0 = raw_contact(MODULE_KEY_AUTO_GPIO, MODULE_KEY_AUTO_ACTIVE_HIGH);
    debounce_init(&s_local_db, local0, MODULE_DEBOUNCE_MS * 1000u);
    debounce_init(&s_auto_db, auto0, MODULE_DEBOUNCE_MS * 1000u);
}

key_position_t keyswitch_hal_read(void)
{
    int64_t now = esp_timer_get_time();
    bool local_raw = raw_contact(MODULE_KEY_LOCAL_GPIO, MODULE_KEY_LOCAL_ACTIVE_HIGH);
    bool auto_raw = raw_contact(MODULE_KEY_AUTO_GPIO, MODULE_KEY_AUTO_ACTIVE_HIGH);

    debounce_update(&s_local_db, local_raw, now);
    debounce_update(&s_auto_db, auto_raw, now);

    return keyswitch_decode(debounce_level(&s_local_db), debounce_level(&s_auto_db));
}
