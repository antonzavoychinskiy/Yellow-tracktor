#include "ui.h"
#include "lvgl_port.h"
#include "config.h"

#include "state_machine_task.h"
#include "control_loop.h"
#include "mission_ui.h"
#include "auto_sequence.h"
#include "telemetry_source.h"
#include "joystick_adc.h"
#include "ardurover_modes.h"

#include "lvgl.h"
#include "lv_font_ru_14.h"

#include <stdio.h>
#include <string.h>

/* ===================== Виджеты ===================== */

static lv_obj_t *s_synthetic_banner;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_warn_banner;
static lv_obj_t *s_content;

static lv_obj_t *s_page_wait_off;
static lv_obj_t *s_lbl_wait_off;

static lv_obj_t *s_page_list;
static lv_obj_t *s_lbl_list_title;
static lv_obj_t *s_lbl_list_rows[MODULE_MAX_ROUTES_UI];

static lv_obj_t *s_page_card;
static lv_obj_t *s_lbl_card_name;
static lv_obj_t *s_lbl_card_hint;
static lv_obj_t *s_lbl_card_progress;

static lv_obj_t *s_page_result;
static lv_obj_t *s_lbl_result;

static lv_obj_t *s_page_local;
static lv_obj_t *s_lbl_local_mode;
static lv_obj_t *s_lbl_local_deadman;
static lv_obj_t *s_lbl_local_ready;
static lv_obj_t *s_lbl_local_battery;

static lv_obj_t *s_page_auto;
static lv_obj_t *s_lbl_auto_mode;
static lv_obj_t *s_lbl_auto_wp;
static lv_obj_t *s_lbl_auto_ready;
static lv_obj_t *s_bar_auto_confirm; /* FR-40.5 */
static lv_obj_t *s_lbl_auto_battery;

/* ===================== Вспомогательное ===================== */

static const char *sm_state_key_text(sm_state_t s)
{
    switch (s) {
    case SM_STATE_WAIT_OFF: return "?";
    case SM_STATE_OFF_WAIT_STOP:
    case SM_STATE_OFF_WAIT_DISARM_CONFIRM:
    case SM_STATE_OFF_IDLE:
    case SM_STATE_OFF_FAILED: return "OFF";
    case SM_STATE_LOCAL_ARMING:
    case SM_STATE_LOCAL_ARM_FAILED:
    case SM_STATE_LOCAL_ACTIVE:
    case SM_STATE_LOCAL_MODE_LOST: return "LOCAL";
    case SM_STATE_AUTO: return "AUTO";
    case SM_STATE_FAULT: return "!!!";
    }
    return "?";
}

static const char *rover_mode_text(uint32_t m)
{
    if (m == ROVER_MODE_MANUAL) return "MANUAL";
    if (m == ROVER_MODE_HOLD) return "HOLD";
    if (m == ROVER_MODE_AUTO) return "AUTO";
    return "?";
}

static const char *mload_err_text(const char *code)
{
    if (strcmp(code, "too_far") == 0) return "первая точка слишком далеко";
    if (strcmp(code, "armed") == 0) return "платформа заармлена";
    if (strcmp(code, "nofile") == 0) return "файл маршрута не найден";
    if (strcmp(code, "parse") == 0) return "ошибка разбора файла";
    if (strcmp(code, "nopos") == 0) return "нет текущей позиции (GPS/EKF)";
    if (strcmp(code, "empty") == 0) return "в файле нет точек";
    if (strcmp(code, "write") == 0) return "ошибка записи миссии";
    if (strcmp(code, "range") == 0) return "номер вне диапазона";
    if (strcmp(code, "crash") == 0) return "внутренняя ошибка скрипта";
    if (strcmp(code, "timeout") == 0) return "нет ответа от Pixhawk";
    return code;
}

static void set_page(lv_obj_t *page)
{
    lv_obj_t *pages[] = { s_page_wait_off, s_page_list, s_page_card, s_page_result,
                           s_page_local, s_page_auto };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        if (pages[i] == page) {
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ===================== Создание страниц ===================== */

/* Шрифт задаётся ЯВНО как локальный стиль на каждой надписи, а не
 * через наследование от экрана/тему — так надёжнее: локальный стиль
 * объекта всегда старше темы/наследования в системе стилей LVGL, не
 * зависит от того, в каком порядке инициализируется тема и создаются
 * объекты. */
static lv_obj_t *mklabel(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_ru_14, 0);
    return lbl;
}

static lv_obj_t *make_page(void)
{
    lv_obj_t *p = lv_obj_create(s_content);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p, 4, 0);
    return p;
}

static void build_pages(void)
{
    /* Ожидание (FR-1.1) */
    s_page_wait_off = make_page();
    s_lbl_wait_off = mklabel(s_page_wait_off);
    lv_label_set_text(s_lbl_wait_off, "Установите ключ в OFF");

    /* Список маршрутов (FR-20..21) */
    s_page_list = make_page();
    s_lbl_list_title = mklabel(s_page_list);
    lv_label_set_text(s_lbl_list_title, "Маршруты");
    for (int i = 0; i < MODULE_MAX_ROUTES_UI; i++) {
        s_lbl_list_rows[i] = mklabel(s_page_list);
        lv_label_set_text(s_lbl_list_rows[i], "");
        lv_obj_add_flag(s_lbl_list_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Карточка маршрута (FR-22, FR-23) */
    s_page_card = make_page();
    s_lbl_card_name = mklabel(s_page_card);
    s_lbl_card_hint = mklabel(s_page_card);
    lv_label_set_text(s_lbl_card_hint,
                       "Удержание OK — загрузить (заменит текущую миссию)"); /* FR-23.1/23.2 */
    lv_label_set_long_mode(s_lbl_card_hint, LV_LABEL_LONG_WRAP);
    s_lbl_card_progress = mklabel(s_page_card);

    /* Результат загрузки (FR-25/26) */
    s_page_result = make_page();
    s_lbl_result = mklabel(s_page_result);
    lv_label_set_long_mode(s_lbl_result, LV_LABEL_LONG_WRAP);

    /* Локальное управление (FR-41) */
    s_page_local = make_page();
    s_lbl_local_mode = mklabel(s_page_local);
    s_lbl_local_deadman = mklabel(s_page_local);
    s_lbl_local_ready = mklabel(s_page_local);
    s_lbl_local_battery = mklabel(s_page_local);

    /* Автономный режим (FR-41) */
    s_page_auto = make_page();
    s_lbl_auto_mode = mklabel(s_page_auto);
    s_lbl_auto_wp = mklabel(s_page_auto);
    s_lbl_auto_ready = mklabel(s_page_auto);
    s_bar_auto_confirm = lv_bar_create(s_page_auto); /* FR-40.5: полоса удержания «Пуск» */
    lv_obj_set_width(s_bar_auto_confirm, LV_PCT(90));
    lv_bar_set_range(s_bar_auto_confirm, 0, 1000);
    lv_obj_add_flag(s_bar_auto_confirm, LV_OBJ_FLAG_HIDDEN);
    s_lbl_auto_battery = mklabel(s_page_auto);
}

esp_err_t ui_init(void)
{
    esp_err_t err = lvgl_port_init();
    if (err != ESP_OK) {
        return err;
    }

    /* Стандартные шрифты LVGL (montserrat) кириллицу не содержат (см.
     * fonts/lv_font_ru_14.c). Шрифт задаётся в двух местах: здесь (в
     * теме — покрывает объекты, создаваемые не через mklabel) и
     * локальным стилем на каждой надписи в mklabel(). Дублирование
     * намеренное: локальный стиль гарантированно старше темы, а тема
     * страхует всё остальное.
     *
     * ВАЖНО: шрифт обязан быть сгенерирован с --no-compress. LVGL по
     * умолчанию собирается с LV_USE_FONT_COMPRESSED=0, и сжатый
     * (bitmap_format=1) шрифт молча рисуется пустотой — проверено на
     * стенде. */
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                               lv_palette_main(LV_PALETTE_RED), false, &lv_font_ru_14);
    lv_disp_set_theme(disp, theme);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);

    /* NFR-17: несъёмный баннер режима отладки — создаётся один раз,
     * видимость никогда не переключается кодом, если синтетический
     * режим включён (компилируется только один из двух telemetry_source). */
    s_synthetic_banner = mklabel(scr);
    lv_label_set_text(s_synthetic_banner, "РЕЖИМ ОТЛАДКИ - ДАННЫЕ СИНТЕТИЧЕСКИЕ");
    lv_obj_set_style_bg_color(s_synthetic_banner, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_bg_opa(s_synthetic_banner, LV_OPA_COVER, 0);
    lv_obj_set_width(s_synthetic_banner, LV_PCT(100));
    lv_obj_set_style_text_align(s_synthetic_banner, LV_TEXT_ALIGN_CENTER, 0);
    if (!telemetry_source_is_synthetic()) {
        lv_obj_add_flag(s_synthetic_banner, LV_OBJ_FLAG_HIDDEN);
    }

    /* FR-42: постоянная строка статуса. */
    s_status_bar = mklabel(scr);
    lv_obj_set_width(s_status_bar, LV_PCT(100));
    lv_label_set_long_mode(s_status_bar, LV_LABEL_LONG_WRAP);

    /* FR-43: критическое предупреждение — высший приоритет отображения. */
    s_warn_banner = mklabel(scr);
    lv_obj_set_style_bg_color(s_warn_banner, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(s_warn_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_warn_banner, lv_color_white(), 0);
    lv_obj_set_width(s_warn_banner, LV_PCT(100));
    lv_label_set_long_mode(s_warn_banner, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_warn_banner, LV_OBJ_FLAG_HIDDEN);

    s_content = lv_obj_create(scr);
    lv_obj_set_size(s_content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    build_pages();
    set_page(s_page_wait_off);
    return ESP_OK;
}

/* ===================== Обновление данных ===================== */

static void update_status_bar(sm_state_t sm, const mavlink_telemetry_snapshot_t *snap)
{
    bool link_ok = telemetry_source_last_heartbeat_age_ms() >= 0 &&
                    telemetry_source_last_heartbeat_age_ms() < MODULE_MAVLINK_LINK_TIMEOUT_MS;
    /* FR-44: явная пометка неактуальности при потере связи. */
    lv_label_set_text_fmt(
        s_status_bar, "Ключ:%s Реж:%s %s Батт:%s%s",
        sm_state_key_text(sm),
        snap->have_heartbeat ? rover_mode_text(snap->heartbeat.custom_mode) : "?",
        snap->have_heartbeat ? (snap->heartbeat.armed ? "ARM" : "disarm") : "?",
        snap->have_sys_status ? "" : "?",
        link_ok ? "" : " [СВЯЗЬ ПОТЕРЯНА]");
}

static void update_warn_banner(sm_state_t sm)
{
    if (sm == SM_STATE_FAULT) {
        lv_label_set_text(s_warn_banner, "ОШИБКА: недопустимое положение ключа");
        lv_obj_clear_flag(s_warn_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (state_machine_get_warn_mode_mismatch()) {
        lv_label_set_text(s_warn_banner, "ВНИМАНИЕ: расхождение режима — HOLD");
        lv_obj_clear_flag(s_warn_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (sm == SM_STATE_LOCAL_ACTIVE &&
               joystick_adc_hal_consecutive_failures() >= MODULE_JOYSTICK_FAIL_THRESHOLD) {
        lv_label_set_text(s_warn_banner, "ОТКАЗ ДЖОЙСТИКА"); /* FR-37.1 */
        lv_obj_clear_flag(s_warn_banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_warn_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_list_page(void)
{
    mission_ui_state_t mst = mission_ui_hal_get_state();

    if (mst == MISSION_UI_REQUESTING) {
        lv_label_set_text(s_lbl_list_title, "Маршруты: запрос...");
    } else if (mst == MISSION_UI_LIST_ERROR) {
        lv_label_set_text(s_lbl_list_title, "Список недоступен. OK — повторить"); /* FR-20.4 */
    } else {
        lv_label_set_text(s_lbl_list_title, "Маршруты (энкодер — выбор, OK — открыть)");
    }

    int count = mission_ui_hal_get_route_count();
    int cursor = mission_ui_hal_get_cursor();
    for (int i = 0; i < MODULE_MAX_ROUTES_UI; i++) {
        if (i >= count) {
            lv_obj_add_flag(s_lbl_list_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        mission_ui_route_t r;
        mission_ui_hal_get_route(i, &r);
        lv_label_set_text_fmt(s_lbl_list_rows[i], "%s%d %s", i == cursor ? "> " : "  ",
                               r.num, r.name);
        lv_obj_clear_flag(s_lbl_list_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_card_page(void)
{
    int num = mission_ui_hal_get_card_route_num();
    lv_label_set_text_fmt(s_lbl_card_name, "Маршрут №%d", num);

    uint32_t progress = mission_ui_hal_get_hold_progress_permille(); /* FR-23.1 */
    if (progress > 0) {
        lv_label_set_text_fmt(s_lbl_card_progress, "Загрузка: %lu%%", (unsigned long)(progress / 10));
    } else {
        lv_label_set_text(s_lbl_card_progress, "");
    }
}

static void update_result_page(void)
{
    mission_ui_load_result_t r;
    if (!mission_ui_hal_get_load_result(&r)) {
        lv_label_set_text(s_lbl_result, "");
        return;
    }
    if (r.load_ok) {
        lv_label_set_text_fmt(s_lbl_result, "Маршрут №%d загружен\nТочек: %d\nДо WP1: %d м\nДлина: %d м",
                               r.load_route_num, r.load_wp_count, r.load_dist_to_wp1_m, r.load_len_m);
    } else {
        lv_label_set_text_fmt(s_lbl_result, "Отказ загрузки маршрута №%d:\n%s",
                               r.load_route_num, mload_err_text(r.load_err_code));
    }
}

static void update_local_page(const mavlink_telemetry_snapshot_t *snap)
{
    lv_label_set_text_fmt(s_lbl_local_mode, "Режим: %s",
                           snap->have_heartbeat ? rover_mode_text(snap->heartbeat.custom_mode) : "?");
    lv_label_set_text_fmt(s_lbl_local_deadman, "Мёртвая рука: %s",
                           control_loop_hal_get_dead_man_engaged() ? "удержана" : "отпущена");
    if (control_loop_hal_get_waiting_initial_neutral()) {
        lv_label_set_text(s_lbl_local_ready, "Верните джойстик в нейтраль"); /* FR-17 */
    } else if (control_loop_hal_get_movement_allowed()) {
        lv_label_set_text(s_lbl_local_ready, "Движение разрешено");
    } else {
        lv_label_set_text(s_lbl_local_ready, "Готов, ждём мёртвую руку");
    }
    if (snap->have_sys_status) {
        lv_label_set_text_fmt(s_lbl_local_battery, "АКБ: %.1f В, %.1f А",
                               snap->battery_voltage_v, snap->battery_current_a);
    } else {
        lv_label_set_text(s_lbl_local_battery, "АКБ: ?");
    }
}

static void update_auto_page(const mavlink_telemetry_snapshot_t *snap)
{
    lv_label_set_text_fmt(s_lbl_auto_mode, "Режим: %s",
                           snap->have_heartbeat ? rover_mode_text(snap->heartbeat.custom_mode) : "?");
    lv_label_set_text_fmt(s_lbl_auto_wp, "Точка маршрута: %u",
                           snap->have_mission_current ? (unsigned)snap->mission_current_seq : 0);

    auto_sequence_state_t seq_state = auto_sequence_hal_get_state();
    switch (seq_state) {
    case AUTO_SEQ_READY: lv_label_set_text(s_lbl_auto_ready, "Готов к пуску — нажмите «Пуск»"); break;
    case AUTO_SEQ_BLOCKED: lv_label_set_text(s_lbl_auto_ready, "Пуск недоступен: маршрут не загружен"); break; /* FR-40.1 */
    case AUTO_SEQ_CONFIRM_HOLD: lv_label_set_text(s_lbl_auto_ready, "Удерживайте «Пуск»..."); break; /* FR-40.5 */
    case AUTO_SEQ_CONFIRM_COUNTDOWN:
        lv_label_set_text_fmt(s_lbl_auto_ready, "Активация Авторежима через %lu",
                               (unsigned long)auto_sequence_hal_get_confirm_countdown_seconds_left()); /* FR-40.6 */
        break;
    case AUTO_SEQ_ARMING: lv_label_set_text(s_lbl_auto_ready, "Арминг..."); break;
    case AUTO_SEQ_ARM_FAILED: lv_label_set_text(s_lbl_auto_ready, "Отказ арминга — нажмите «Пуск» ещё раз"); break;
    case AUTO_SEQ_SETTING_MODE: lv_label_set_text(s_lbl_auto_ready, "Перевод в AUTO..."); break;
    case AUTO_SEQ_MOVING: lv_label_set_text(s_lbl_auto_ready, "Движение по маршруту"); break;
    }

    if (seq_state == AUTO_SEQ_CONFIRM_HOLD) {
        lv_bar_set_value(s_bar_auto_confirm, (int32_t)auto_sequence_hal_get_confirm_hold_progress_permille(),
                          LV_ANIM_OFF);
        lv_obj_clear_flag(s_bar_auto_confirm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bar_auto_confirm, LV_OBJ_FLAG_HIDDEN);
    }

    if (snap->have_sys_status) {
        lv_label_set_text_fmt(s_lbl_auto_battery, "АКБ: %.1f В, %.1f А",
                               snap->battery_voltage_v, snap->battery_current_a);
    } else {
        lv_label_set_text(s_lbl_auto_battery, "АКБ: ?");
    }
}

void ui_tick(void)
{
    sm_state_t sm = state_machine_get_state();
    mavlink_telemetry_snapshot_t snap;
    telemetry_source_get_telemetry_snapshot(&snap);

    update_status_bar(sm, &snap);
    update_warn_banner(sm);

    switch (sm) {
    case SM_STATE_WAIT_OFF:
        set_page(s_page_wait_off);
        break;

    case SM_STATE_OFF_WAIT_STOP:
    case SM_STATE_OFF_WAIT_DISARM_CONFIRM:
    case SM_STATE_OFF_IDLE:
    case SM_STATE_OFF_FAILED: {
        mission_ui_state_t mst = mission_ui_hal_get_state();
        if (mst == MISSION_UI_CARD) {
            update_card_page();
            set_page(s_page_card);
        } else if (mst == MISSION_UI_LOADING || mst == MISSION_UI_LOAD_RESULT) {
            update_result_page();
            set_page(s_page_result);
        } else {
            update_list_page();
            set_page(s_page_list);
        }
        break;
    }

    case SM_STATE_LOCAL_ARMING:
    case SM_STATE_LOCAL_ARM_FAILED:
    case SM_STATE_LOCAL_ACTIVE:
    case SM_STATE_LOCAL_MODE_LOST:
        update_local_page(&snap);
        set_page(s_page_local);
        break;

    case SM_STATE_AUTO:
        update_auto_page(&snap);
        set_page(s_page_auto);
        break;

    case SM_STATE_FAULT:
        /* Баннер (update_warn_banner) уже показывает причину;
         * содержательная страница не важна — оставляем последнюю. */
        break;
    }

    lvgl_port_tick_and_handle();
}
