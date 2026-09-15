#include "lvgl_port.h"
#include "config.h"
#include "display.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "lvgl.h"

/*
 * Интеграция LVGL поверх esp_lcd-панели, инициализированной в
 * components/display (этап 1). Буфер отрисовки — частичный (не под
 * весь экран). esp_lcd_panel_draw_bitmap() по SPI асинхронен — он
 * ставит передачу в очередь DMA и возвращается сразу, не дожидаясь
 * реальной отправки байт. lv_disp_flush_ready() поэтому нельзя звать
 * сразу после draw_bitmap(): LVGL тут же начнёт писать в тот же буфер
 * следующий кусок кадра, и старая передача уедет на экран пополам с
 * новыми данными — визуально это выглядит как "снег"/шум. Поэтому
 * flush_ready вызывается только из колбэка on_color_trans_done,
 * который esp_lcd вызывает по факту завершения передачи.
 *
 * lv_tick_inc() кормится вручную из ui_tick() (единый цикл уровня
 * интерфейса, NFR-1..3) — отдельный тикающий таймер не заводится.
 */

static const char *TAG = "lvgl_port";

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t *s_buf1;

#define LVGL_BUF_ROWS 40 /* высота полосы буфера, строк */

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = display_hal_panel();
    /* +1: esp_lcd ожидает границы включительно/исключительно по
     * x2/y2 — область LVGL включительна, esp_lcd — исключительна.
     * lv_disp_flush_ready() здесь НЕ вызывается — см. комментарий
     * выше и on_color_trans_done(). */
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx)
{
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

esp_err_t lvgl_port_init(void)
{
    lv_init();

    size_t buf_pixels = (size_t)MODULE_DISPLAY_WIDTH * LVGL_BUF_ROWS;
    s_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (s_buf1 == NULL) {
        ESP_LOGE(TAG, "no memory for LVGL draw buffer");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = MODULE_DISPLAY_WIDTH;
    s_disp_drv.ver_res = MODULE_DISPLAY_HEIGHT;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    esp_err_t cb_err = esp_lcd_panel_io_register_event_callbacks(
        display_hal_panel_io(), &io_cbs, &s_disp_drv);
    if (cb_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_io_register_event_callbacks failed: %s",
                 esp_err_to_name(cb_err));
        return cb_err;
    }

    ESP_LOGI(TAG, "LVGL initialized (%dx%d, buffer %d rows)",
             MODULE_DISPLAY_WIDTH, MODULE_DISPLAY_HEIGHT, LVGL_BUF_ROWS);
    return ESP_OK;
}

void lvgl_port_tick_and_handle(void)
{
    static int64_t last_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (last_us != 0) {
        uint32_t elapsed_ms = (uint32_t)((now_us - last_us) / 1000);
        if (elapsed_ms > 0) {
            lv_tick_inc(elapsed_ms);
        }
    }
    last_us = now_us;

    lv_timer_handler();
}
