#pragma once

#include "esp_err.h"

esp_err_t lvgl_port_init(void);

/* Кормит lv_tick_inc() по факту прошедшего времени и вызывает
 * lv_timer_handler(). Вызывать раз за тик из ui_task. */
void lvgl_port_tick_and_handle(void);
