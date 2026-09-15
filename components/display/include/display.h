#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/*
 * Аппаратный bring-up дисплея ST7789 (SPI) — только инициализация
 * панели. Привязка к LVGL (буферы, flush-колбэк, объекты экранов)
 * делается на этапе 8, в компоненте ui.
 *
 * ПРИМЕЧАНИЕ: в требованиях (п. 3.2) нет отдельного GPIO подсветки —
 * предполагается, что подсветка запитана аппаратно напрямую (всегда
 * включена), программное управление не реализуется. Если это не так —
 * потребуется добавить GPIO подсветки в config.h.
 */

esp_err_t display_hal_init(void);

/* Хэндл панели для дальнейшей привязки к LVGL (этап 8). */
esp_lcd_panel_handle_t display_hal_panel(void);

/* Хэндл IO (SPI) — нужен LVGL-обвязке, чтобы подписаться на
 * on_color_trans_done и не отдавать буфер под следующий кадр раньше,
 * чем реально завершится передача текущего (см. lvgl_port.c). */
esp_lcd_panel_io_handle_t display_hal_panel_io(void);
