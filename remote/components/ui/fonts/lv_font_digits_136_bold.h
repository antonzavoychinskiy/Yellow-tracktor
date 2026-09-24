#pragma once

/*
 * Шрифт для крупной цифры обратного отсчёта (185пт запроса к
 * lv_font_conv, реальная высота глифа цифры ~131-136px, 4bpp, только
 * 0-9), сгенерирован из локального Arial Bold (Windows). Только
 * цифры — экран AUTO_SEQ_CONFIRM_COUNTDOWN ничего, кроме числа
 * секунд, не показывает, кириллица не нужна. Высота глифа подобрана
 * так, чтобы цифра занимала ~80% высоты экрана 320x170
 * (MODULE_DISPLAY_HEIGHT, config.h) на полноэкранном синем фоне —
 * из-за нелинейности метрик TTF-шрифта запрошенный --size (185)
 * заметно больше итоговой пиксельной высоты глифа.
 *
 * Команда генерации (флаг --no-compress ОБЯЗАТЕЛЕН — см. lv_font_ru_14.h):
 *
 *   npx lv_font_conv --font <arialbd.ttf> --size 185 --bpp 4 --no-compress \
 *       -r 0x30-0x39 \
 *       --format lvgl --lv-font-name lv_font_digits_136_bold -o lv_font_digits_136_bold.c
 */

#include "lvgl.h"

extern const lv_font_t lv_font_digits_136_bold;
