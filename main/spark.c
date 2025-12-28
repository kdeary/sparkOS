#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"

#include "engine/spark_engine.h"
#include "main.h"

#include "spark_cart.h"
#include "spark_config.h"
#include "spark_device.h"
#include "spark_gui.h"
#include "spark_utils.h"

#define TAG "spark_firmware"

typedef struct SparkAppContext {
    SparkRuntime runtime;
    SparkStaticReader reader;
    uint16_t line_buffer[SCREEN_WIDTH];
    uint32_t frame_counter;
    const uint8_t *cart_blob;
    size_t cart_blob_size;
    esp_partition_mmap_handle_t cart_mmap_handle;
    char cart_path[256];
} SparkAppContext;

void app_main(void)
{
    SparkAppContext app_state = {0};
    SparkAppContext *app = &app_state;
    const char *error = NULL;
    char err_detail[96] = {0};

    ESP_LOGI(TAG, "spark! loading...");

    if (!spark_device_init_display()) {
        ESP_LOGE(TAG, "LCD init failed");
        return;
    }
    ESP_LOGI(TAG, "LCD ready");

    if (CONFIG_LCD_WIDTH != SCREEN_WIDTH || CONFIG_LCD_HEIGHT != SCREEN_HEIGHT) {
        ESP_LOGW(TAG, "LCD config %dx%d does not match runtime %dx%d",
                 CONFIG_LCD_WIDTH, CONFIG_LCD_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    ESP_LOGI(TAG, "Mounting filesystem");
    if (spark_device_mount_filesystem() != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed");
        spark_gui_draw_error_screen("SD MOUNT FAIL", NULL);
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    ESP_LOGI(TAG, "Reading boot cart config");
    if (!spark_cart_load_boot_path(app->cart_path, sizeof(app->cart_path), err_detail, sizeof(err_detail))) {
        ESP_LOGE(TAG, "No cartridge configured; add %s/%s (%s)",
                 SPARK_FILESYSTEM_BASE_PATH, SPARK_BOOT_CONFIG_FILE, err_detail);
        spark_gui_draw_error_screen("NO CART CONFIG", err_detail);
        return;
    }
    ESP_LOGI(TAG, "Boot cart path: %s", app->cart_path);

    memset(err_detail, 0, sizeof(err_detail));
    ESP_LOGI(TAG, "Loading cart into flash");
    app->cart_blob = spark_cart_load_to_partition(app->cart_path,
                                                  &app->cart_blob_size,
                                                  &app->cart_mmap_handle,
                                                  err_detail, sizeof(err_detail));
    if (!app->cart_blob) {
        ESP_LOGE(TAG, "Failed to load cartridge: %s (%s)", app->cart_path, err_detail);
        spark_gui_draw_error_screen("CART LOAD FAIL", err_detail[0] ? err_detail : app->cart_path);
        return;
    }
    ESP_LOGI(TAG, "Cart loaded (%u bytes)", (unsigned)app->cart_blob_size);
    spark_device_unmount_filesystem();

    memset(err_detail, 0, sizeof(err_detail));
    ESP_LOGI(TAG, "Validating cart");
    if (!spark_cart_validate_image(app->cart_blob, app->cart_blob_size, err_detail, sizeof(err_detail))) {
        ESP_LOGE(TAG, "Invalid cartridge image: %s", err_detail);
        spark_gui_draw_error_screen("CART INVALID", err_detail);
        esp_partition_munmap(app->cart_mmap_handle);
        return;
    }
    ESP_LOGI(TAG, "Cart validated");

    spark_log_memory_snapshot("before wasm init");
    esp_rom_printf("[boot] runtime load start\n");
    error = spark_runtime_load_sprk(&app->runtime, app->cart_blob, (uint32_t)app->cart_blob_size);
    if (error) {
        esp_rom_printf("Runtime init failed: %s\n", error);
        spark_gui_draw_error_screen("RUNTIME INIT FAIL", error);
        esp_partition_munmap(app->cart_mmap_handle);
        return;
    }
    esp_rom_printf("[boot] runtime load done\n");
    esp_rom_printf("[boot] cart wasm=%" PRIu32 " static_off=%" PRIu32 " static_size=%" PRIu32 "\n",
                   app->runtime.cart.wasm_size,
                   app->runtime.engine.static_memory.offset,
                   app->runtime.engine.static_memory.size);

    app->reader.data = app->cart_blob;
    app->reader.size = (uint32_t)app->cart_blob_size;
    app->reader.static_offset = app->runtime.engine.static_memory.offset;
    app->reader.static_size = app->runtime.engine.static_memory.size;
    spark_engine_set_static_reader(&app->runtime.engine, spark_read_static_memory, &app->reader);

    esp_rom_printf("[boot] starting runtime\n");
    error = spark_engine_run_game(&app->runtime.engine, &app->runtime.wasm);
    if (error) {
        esp_rom_printf("Runtime start failed: %s\n", error);
        spark_gui_draw_error_screen("RUNTIME START FAIL", error);
        spark_runtime_deinit(&app->runtime);
        esp_partition_munmap(app->cart_mmap_handle);
        return;
    }
    esp_rom_printf("[boot] runtime started\n");
    spark_log_memory_snapshot("after runtime start");
    esp_rom_printf("[boot] running cart: %s\n", app->cart_path);

    while (1) {
        error = spark_engine_step(&app->runtime.engine, &app->runtime.wasm);
        if (error) {
            esp_rom_printf("Runtime step failed: %s\n", error);
            spark_gui_draw_error_screen("RUNTIME STEP FAIL", error);
            break;
        }

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            uint32_t row = (uint32_t)y * SCREEN_WIDTH;
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                uint8_t color_index = app->runtime.engine.framebuffer[row + x];
                uint16_t color = app->runtime.engine.palette[color_index];
#if CONFIG_LCD_COLOR_SPACE_BGR
                color = spark_color_rgb_to_bgr(color);
#endif
                app->line_buffer[x] = spark_color_swap_bytes(color);
            }
            spark_device_draw_bitmap(0, y, SCREEN_WIDTH, y + 1, app->line_buffer);
        }

        app->frame_counter++;
        esp_rom_delay_us(16000);
    }

    spark_runtime_deinit(&app->runtime);
    esp_partition_munmap(app->cart_mmap_handle);
}
