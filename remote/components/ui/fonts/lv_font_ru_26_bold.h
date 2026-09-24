#pragma once

/*
 * Кириллический полужирный шрифт для LVGL (26пт, 4bpp), сгенерирован из
 * локального Arial Bold (Windows) через lv_font_conv — те же диапазоны
 * символов, что и lv_font_ru_14.h. Используется только там, где нужна
 * крупная надпись на всю ширину экрана (например, «Удерживаете «Пуск»»
 * на экране AUTO_SEQ_CONFIRM_HOLD).
 *
 * Команда генерации (флаг --no-compress ОБЯЗАТЕЛЕН — см. lv_font_ru_14.h):
 *
 *   npx lv_font_conv --font <arialbd.ttf> --size 26 --bpp 4 --no-compress \
 *       -r 0x20-0x7E -r 0xAB -r 0xBB -r 0x400-0x4FF -r 0x2014 -r 0x2116 \
 *       --format lvgl --lv-font-name lv_font_ru_26_bold -o lv_font_ru_26_bold.c
 */

#include "lvgl.h"

extern const lv_font_t lv_font_ru_26_bold;
