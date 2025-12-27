#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main.h"

#define TAG "spark_firmware"

#define FILESYSTEM_BASE_PATH "/carts"
#define BOOT_CONFIG_FILE ".spark"

/* ESP32 LCD defaults (override via sdkconfig if needed). */
#ifndef CONFIG_LCD_WIDTH
#define CONFIG_LCD_WIDTH 320
#endif
#ifndef CONFIG_LCD_HEIGHT
#define CONFIG_LCD_HEIGHT 240
#endif
#ifndef CONFIG_LCD_OFFSET_X
#define CONFIG_LCD_OFFSET_X 0
#endif
#ifndef CONFIG_LCD_OFFSET_Y
#define CONFIG_LCD_OFFSET_Y 0
#endif
#ifndef CONFIG_LCD_SCLK_GPIO
#define CONFIG_LCD_SCLK_GPIO 12
#endif
#ifndef CONFIG_LCD_DC_GPIO
#define CONFIG_LCD_DC_GPIO 46
#endif
#ifndef CONFIG_LCD_CS_GPIO
#define CONFIG_LCD_CS_GPIO 10
#endif
#ifndef CONFIG_LCD_MISO_GPIO
#define CONFIG_LCD_MISO_GPIO 13
#endif
#ifndef CONFIG_LCD_MOSI_GPIO
#define CONFIG_LCD_MOSI_GPIO 11
#endif
#ifndef CONFIG_LCD_BL_GPIO
#define CONFIG_LCD_BL_GPIO 45
#endif
#ifndef CONFIG_LCD_RST_GPIO
#define CONFIG_LCD_RST_GPIO -1
#endif
#ifndef CONFIG_LCD_PIXEL_CLOCK
#define CONFIG_LCD_PIXEL_CLOCK (40 * 1000 * 1000)
#endif
#ifndef CONFIG_LCD_BL_ACTIVE_HIGH
#define CONFIG_LCD_BL_ACTIVE_HIGH 1
#endif
#ifndef CONFIG_LCD_SWAP_XY
#define CONFIG_LCD_SWAP_XY 1
#endif
#ifndef CONFIG_LCD_MIRROR_X
#define CONFIG_LCD_MIRROR_X 0
#endif
#ifndef CONFIG_LCD_MIRROR_Y
#define CONFIG_LCD_MIRROR_Y 0
#endif
#ifndef CONFIG_LCD_INVERT_COLORS
#define CONFIG_LCD_INVERT_COLORS 0
#endif
#ifndef CONFIG_LCD_COLOR_SPACE_BGR
#define CONFIG_LCD_COLOR_SPACE_BGR 1
#endif
#ifndef CONFIG_SD_SPI_MOSI_GPIO
#define CONFIG_SD_SPI_MOSI_GPIO 23
#endif
#ifndef CONFIG_SD_SPI_MISO_GPIO
#define CONFIG_SD_SPI_MISO_GPIO 19
#endif
#ifndef CONFIG_SD_SPI_CLK_GPIO
#define CONFIG_SD_SPI_CLK_GPIO 18
#endif
#ifndef CONFIG_SD_SPI_CS_GPIO
#define CONFIG_SD_SPI_CS_GPIO 5
#endif
#ifndef CONFIG_SD_SPI_HOST
#define CONFIG_SD_SPI_HOST SPI3_HOST
#endif
#define LCD_HOST SPI2_HOST

typedef struct SparkStaticReader {
    const uint8_t *data;
    uint32_t size;
    uint32_t static_offset;
    uint32_t static_size;
} SparkStaticReader;

static esp_lcd_panel_handle_t s_panel = NULL;
static sdmmc_card_t *s_sd_card = NULL;

static esp_lcd_panel_handle_t init_display(void);
static void display_draw_bitmap(int xs, int ys, int xe, int ye, const uint16_t *data);
static esp_err_t mount_filesystem(void);
static bool load_boot_cart_path(char *out, size_t out_size);
static uint8_t *read_file(const char *path, size_t *out_size);

static int spark_read_static_memory(uint32_t index, uint32_t size, uint8_t *out, void *userdata)
{
    SparkStaticReader *reader = (SparkStaticReader *)userdata;
    const uint8_t *src = NULL;

    if (!reader || !out) {
        return 0;
    }

    if (index > reader->static_size || size > reader->static_size - index) {
        return 0;
    }

    src = reader->data + reader->static_offset + index;
    memcpy(out, src, size);
    return 1;
}

static char *trim(char *str)
{
    while (isspace((unsigned char)*str)) {
        str++;
    }
    size_t len = strlen(str);
    while (len && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
    return str;
}

static bool load_boot_cart_path(char *out, size_t out_size)
{
    char cfg_path[256];
    FILE *file = NULL;
    char line[256];

    if (!out || out_size == 0) {
        return false;
    }

    snprintf(cfg_path, sizeof(cfg_path), "%s/%s", FILESYSTEM_BASE_PATH, BOOT_CONFIG_FILE);
    file = fopen(cfg_path, "r");
    if (!file) {
        ESP_LOGW(TAG, "Boot config not found: %s", cfg_path);
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *entry = trim(line);
        const char *value = NULL;

        if (!entry[0] || entry[0] == '#') {
            continue;
        }

        if (strncmp(entry, "CART_FILE=", 10) == 0) {
            value = entry + 10;
        } else if (strncmp(entry, "ROM_FILE=", 9) == 0) {
            value = entry + 9;
        } else {
            continue;
        }

        value = trim((char *)value);
        if (!value[0]) {
            continue;
        }

        if (value[0] == '/') {
            snprintf(out, out_size, "%s", value);
        } else {
            snprintf(out, out_size, "%s/%s", FILESYSTEM_BASE_PATH, value);
        }
        fclose(file);
        return true;
    }

    fclose(file);
    return false;
}

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data = NULL;
    size_t size = 0;
    long length = 0;

    if (!file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return NULL;
    }
    size = (size_t)length;

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (!data) {
        data = (uint8_t *)malloc(size);
    }
    if (!data) {
        fclose(file);
        return NULL;
    }

    if (fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return NULL;
    }

    fclose(file);
    if (out_size) {
        *out_size = size;
    }
    return data;
}

static uint16_t color_rgb_to_bgr(uint16_t rgb)
{
    uint16_t r = (rgb >> 11) & 0x1F;
    uint16_t g = (rgb >> 5) & 0x3F;
    uint16_t b = rgb & 0x1F;
    return (uint16_t)((b << 11) | (g << 5) | r);
}

static uint16_t color_swap_bytes(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static esp_lcd_panel_handle_t init_display(void)
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
        .color_space = CONFIG_LCD_COLOR_SPACE_BGR ? ESP_LCD_COLOR_SPACE_BGR : ESP_LCD_COLOR_SPACE_RGB,
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

    return s_panel;
}

static void display_draw_bitmap(int xs, int ys, int xe, int ye, const uint16_t *data)
{
    if (!s_panel || !data) {
        return;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, xs, ys, xe, ye, data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD draw failed (%s)", esp_err_to_name(err));
    }
}

static esp_err_t mount_filesystem(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SD_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_SD_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_SD_SPI_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(CONFIG_SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed for SD (%s)", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_SPI_CS_GPIO;
    slot_config.host_id = CONFIG_SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = CONFIG_SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Mounting SD card at %s", FILESYSTEM_BASE_PATH);
    ret = esp_vfs_fat_sdspi_mount(FILESYSTEM_BASE_PATH, &host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

void app_main(void)
{
    SparkRuntime runtime;
    SparkStaticReader reader;
    const char *error = NULL;
    char cart_path[256] = {0};
    uint8_t *cart_blob = NULL;
    size_t cart_blob_size = 0;
    uint16_t line_buffer[SCREEN_WIDTH];

    if (!init_display()) {
        ESP_LOGE(TAG, "LCD init failed");
        return;
    }

    if (CONFIG_LCD_WIDTH != SCREEN_WIDTH || CONFIG_LCD_HEIGHT != SCREEN_HEIGHT) {
        ESP_LOGW(TAG, "LCD config %dx%d does not match runtime %dx%d",
                 CONFIG_LCD_WIDTH, CONFIG_LCD_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    if (mount_filesystem() != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed");
        return;
    }

    if (!load_boot_cart_path(cart_path, sizeof(cart_path))) {
        ESP_LOGE(TAG, "No cartridge configured; add %s/%s", FILESYSTEM_BASE_PATH, BOOT_CONFIG_FILE);
        return;
    }

    cart_blob = read_file(cart_path, &cart_blob_size);
    if (!cart_blob) {
        ESP_LOGE(TAG, "Failed to load cartridge: %s", cart_path);
        return;
    }

    error = spark_runtime_load_sprk(&runtime, cart_blob, (uint32_t)cart_blob_size);
    if (error) {
        ESP_LOGE(TAG, "Runtime init failed: %s", error);
        free(cart_blob);
        return;
    }

    reader.data = cart_blob;
    reader.size = (uint32_t)cart_blob_size;
    reader.static_offset = runtime.engine.static_memory.offset;
    reader.static_size = runtime.engine.static_memory.size;
    spark_engine_set_static_reader(&runtime.engine, spark_read_static_memory, &reader);

    error = spark_engine_run_game(&runtime.engine, &runtime.wasm);
    if (error) {
        ESP_LOGE(TAG, "Runtime start failed: %s", error);
        spark_runtime_deinit(&runtime);
        free(cart_blob);
        return;
    }

    ESP_LOGI(TAG, "Running cart: %s", cart_path);

    while (1) {
        error = spark_engine_step(&runtime.engine, &runtime.wasm);
        if (error) {
            ESP_LOGE(TAG, "Runtime step failed: %s", error);
            break;
        }

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            uint32_t row = (uint32_t)y * SCREEN_WIDTH;
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                uint8_t color_index = runtime.engine.framebuffer[row + x];
                uint16_t color = runtime.engine.palette[color_index];
#if CONFIG_LCD_COLOR_SPACE_BGR
                color = color_rgb_to_bgr(color);
#endif
                line_buffer[x] = color_swap_bytes(color);
            }
            display_draw_bitmap(0, y, SCREEN_WIDTH, y + 1, line_buffer);
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }

    spark_runtime_deinit(&runtime);
    free(cart_blob);
}
