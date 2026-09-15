#pragma once

/*
 * Кириллический шрифт для LVGL (14пт, 4bpp), сгенерирован из локального
 * Arial (Windows) через lv_font_conv. Стандартные шрифты LVGL
 * (lv_font_montserrat_*) кириллицу не содержат — см. lv_font_ru_14.c.
 *
 * Диапазоны: 0x20-0x7E (латиница/цифры/пунктуация), 0x400-0x4FF
 * (кириллица), плюс точечно 0xAB/0xBB («»), 0x2014 (—), 0x2116 (№) —
 * эти символы встречаются в текстах экранов и без них LVGL рисует
 * пустой прямоугольник. При добавлении в UI новых символов вне этих
 * диапазонов шрифт нужно перегенерировать.
 *
 * Команда генерации (флаг --no-compress ОБЯЗАТЕЛЕН: сборка LVGL идёт с
 * LV_USE_FONT_COMPRESSED=0, и сжатый шрифт рисуется пустотой без
 * единой ошибки — проверено на стенде):
 *
 *   npx lv_font_conv --font <ttf> --size 14 --bpp 4 --no-compress \
 *       -r 0x20-0x7E -r 0xAB -r 0xBB -r 0x400-0x4FF -r 0x2014 -r 0x2116 \
 *       --format lvgl --lv-font-name lv_font_ru_14 -o lv_font_ru_14.c
 */

#include "lvgl.h"

extern const lv_font_t lv_font_ru_14;
