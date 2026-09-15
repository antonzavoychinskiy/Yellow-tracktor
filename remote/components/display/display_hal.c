#include "display.h"
#include "config.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_io_spi.h"
#include "esp_log.h"

/*
 * Проверено сборкой на ESP-IDF 6.0.1 (framework-espidf 4.60001.0):
 * esp_lcd_panel_io_spi_config_t/esp_lcd_new_panel_io_spi — заголовок
 * esp_lcd_io_spi.h (не esp_lcd_panel_io.h); esp_lcd_panel_dev_config_t
 * больше не содержит .color_space — вместо него .rgb_ele_order.
 */

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;

esp_err_t display_hal_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = MODULE_DISPLAY_SCK_GPIO,
        .mosi_io_num = MODULE_DISPLAY_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MODULE_DISPLAY_WIDTH * MODULE_DISPLAY_HEIGHT * 2,
    };
    esp_err_t err = spi_bus_initialize(MODULE_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = MODULE_DISPLAY_DC_GPIO,
        .cs_gpio_num = MODULE_DISPLAY_CS_GPIO,
        .pclk_hz = MODULE_DISPLAY_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        /* =1: строго одна передача "в полёте". С большей глубиной
         * очереди on_color_trans_done мог срабатывать раньше, чем
         * реально завершались ВСЕ SPI-транзакции конкретной области
         * кадра (esp_lcd режет большую область на несколько
         * транзакций) — LVGL получал "буфер свободен" преждевременно
         * и переписывал его следующим куском поверх ещё не отправленных
         * данных. Визуально это давало полосы/шум. */
        .trans_queue_depth = 1,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)MODULE_SPI_HOST, &io_config, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = MODULE_DISPLAY_RES_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true); /* типично требуется для матриц ST7789 */

    /* Поворот в альбомную ориентацию (физическая матрица 170x320,
     * логически используем как 320x170) — см. config.h. */
    esp_lcd_panel_swap_xy(s_panel, MODULE_DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, MODULE_DISPLAY_MIRROR_X, MODULE_DISPLAY_MIRROR_Y);
    esp_lcd_panel_set_gap(s_panel, MODULE_DISPLAY_GAP_X, MODULE_DISPLAY_GAP_Y);

    esp_lcd_panel_disp_on_off(s_panel, true);

    ESP_LOGI(TAG, "ST7789 panel initialized (%dx%d, phys %dx%d)",
             MODULE_DISPLAY_WIDTH, MODULE_DISPLAY_HEIGHT,
             MODULE_DISPLAY_PHYS_WIDTH, MODULE_DISPLAY_PHYS_HEIGHT);
    return ESP_OK;
}

esp_lcd_panel_handle_t display_hal_panel(void)
{
    return s_panel;
}

esp_lcd_panel_io_handle_t display_hal_panel_io(void)
{
    return s_io;
}
