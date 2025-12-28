#include "spark_device.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "spark_cart.h"
#include "spark_config.h"

#define TAG "spark_firmware"
#define LCD_HOST SPI2_HOST

static esp_lcd_panel_handle_t s_panel = NULL;
static sdmmc_card_t *s_sd_card = NULL;

bool spark_device_init_display(void)
{
    ESP_LOGI(TAG,
             "LCD config: %dx%d offset(%d,%d) pins SCLK=%d MOSI=%d MISO=%d DC=%d CS=%d RST=%d BL=%d clk=%d",
             CONFIG_LCD_WIDTH,
             CONFIG_LCD_HEIGHT,
             CONFIG_LCD_OFFSET_X,
             CONFIG_LCD_OFFSET_Y,
             CONFIG_LCD_SCLK_GPIO,
             CONFIG_LCD_MOSI_GPIO,
             CONFIG_LCD_MISO_GPIO,
             CONFIG_LCD_DC_GPIO,
             CONFIG_LCD_CS_GPIO,
             CONFIG_LCD_RST_GPIO,
             CONFIG_LCD_BL_GPIO,
             CONFIG_LCD_PIXEL_CLOCK);

    spi_bus_config_t bus_config = {
        .sclk_io_num = CONFIG_LCD_SCLK_GPIO,
        .mosi_io_num = CONFIG_LCD_MOSI_GPIO,
        .miso_io_num = CONFIG_LCD_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_LCD_WIDTH * CONFIG_LCD_HEIGHT * sizeof(uint16_t),
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CONFIG_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_LCD_CS_GPIO,
        .pclk_hz = CONFIG_LCD_PIXEL_CLOCK,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_LCD_RST_GPIO,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
        .vendor_config = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &s_panel));

#if CONFIG_LCD_BL_GPIO >= 0
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << CONFIG_LCD_BL_GPIO),
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(CONFIG_LCD_BL_GPIO, CONFIG_LCD_BL_ACTIVE_HIGH ? 1 : 0);
    }
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, CONFIG_LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, CONFIG_LCD_MIRROR_X, CONFIG_LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, CONFIG_LCD_INVERT_COLORS));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, CONFIG_LCD_OFFSET_X, CONFIG_LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    return s_panel != NULL;
}

bool spark_device_display_ready(void)
{
    return s_panel != NULL;
}

void spark_device_draw_bitmap(int xs, int ys, int xe, int ye, const uint16_t *data)
{
    if (!s_panel || !data) {
        return;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, xs, ys, xe, ye, data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD draw failed (%s)", esp_err_to_name(err));
    }
}

esp_err_t spark_device_mount_filesystem(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    slot_config.width = 4;
    slot_config.clk = CONFIG_SDMMC_CLK_GPIO;
    slot_config.cmd = CONFIG_SDMMC_CMD_GPIO;
    slot_config.d0 = CONFIG_SDMMC_D0_GPIO;
    slot_config.d1 = CONFIG_SDMMC_D1_GPIO;
    slot_config.d2 = CONFIG_SDMMC_D2_GPIO;
    slot_config.d3 = CONFIG_SDMMC_D3_GPIO;
    slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting SD card at %s", SPARK_FILESYSTEM_BASE_PATH);
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SPARK_FILESYSTEM_BASE_PATH,
                                           &host,
                                           &slot_config,
                                           &mount_config,
                                           &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

void spark_device_unmount_filesystem(void)
{
    if (s_sd_card) {
        ESP_LOGI(TAG, "Unmounting SD card at %s", SPARK_FILESYSTEM_BASE_PATH);
        esp_vfs_fat_sdcard_unmount(SPARK_FILESYSTEM_BASE_PATH, s_sd_card);
        s_sd_card = NULL;
    }
}
