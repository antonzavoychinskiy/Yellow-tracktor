#pragma once

#include "esp_err.h"

/*
 * Единая задача уровня интерфейса (NFR-1..3): меню/список маршрутов
 * (mission_ui, этап 6), позже — кнопка «Пуск» (auto_sequence, этап 7)
 * и отрисовка LVGL (этап 8). Одно ядро (1), не блокирует RT-уровень.
 */
esp_err_t ui_task_start(void);
