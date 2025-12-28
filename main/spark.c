#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
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
    SparkCartImage cart;
    uint16_t line_buffer[SCREEN_WIDTH];
    uint32_t frame_counter;
    char cart_path[256];
} SparkAppContext;

static SparkAppContext s_app;

static void *spark_wamr_psram_malloc(unsigned int size)
{
    return heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void *spark_wamr_psram_realloc(void *ptr, unsigned int size)
{
    return heap_caps_realloc(ptr, (size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void spark_wamr_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

static uint32_t spark_wasm_read_uleb(const uint8_t *data, uint32_t size, uint32_t *cursor)
{
    uint32_t result = 0;
    uint32_t shift = 0;

    while (*cursor < size && shift < 32) {
        uint8_t byte = data[(*cursor)++];
        result |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
    }

    return result;
}

static uint32_t spark_wasm_find_initial_memory(const uint8_t *data,
                                               uint32_t size,
                                               uint32_t *out_min_pages,
                                               uint32_t *out_max_pages,
                                               uint32_t *out_flags,
                                               uint32_t *out_count)
{
    const uint8_t *p = data;
    uint32_t offset = 0;
    uint32_t payload_len = 0;

    if (out_min_pages) {
        *out_min_pages = 0;
    }
    if (out_max_pages) {
        *out_max_pages = 0;
    }
    if (out_flags) {
        *out_flags = 0;
    }
    if (out_count) {
        *out_count = 0;
    }

    if (!data || size < 8) {
        return 0;
    }

    if (!(p[0] == 0x00 && p[1] == 0x61 && p[2] == 0x73 && p[3] == 0x6d)) {
        return 0;
    }

    offset = 8;
    while (offset < size) {
        uint8_t section_id = p[offset++];
        if (offset >= size) {
            return 0;
        }

        payload_len = spark_wasm_read_uleb(p, size, &offset);
        if (payload_len == 0 || offset + payload_len > size) {
            return 0;
        }

        if (section_id == 5) {
            uint32_t section_end = offset + payload_len;
            uint32_t count = spark_wasm_read_uleb(p, size, &offset);
            if (out_count) {
                *out_count = count;
            }
            if (count == 0) {
                return 0;
            }

            (void)count;
            {
                uint32_t flags = spark_wasm_read_uleb(p, size, &offset);
                uint32_t min_pages = spark_wasm_read_uleb(p, size, &offset);
                uint32_t initial_bytes = min_pages * 64u * 1024u;

                if (flags & 0x01) {
                    uint32_t max_pages = spark_wasm_read_uleb(p, size, &offset);
                    if (out_max_pages) {
                        *out_max_pages = max_pages;
                    }
                }

                if (offset > section_end) {
                    return 0;
                }

                if (out_min_pages) {
                    *out_min_pages = min_pages;
                }
                if (out_flags) {
                    *out_flags = flags;
                }
                return initial_bytes;
            }
        }

        offset += payload_len;
    }

    return 0;
}

static void *spark_app_thread(void *arg)
{
    (void)arg;
    SparkAppContext *app = &s_app;
    const char *error = NULL;
    char err_detail[96] = {0};

    memset(app, 0, sizeof(*app));

    ESP_LOGI(TAG, "spark! loading...");

    if (!spark_device_init_display()) {
        ESP_LOGE(TAG, "LCD init failed");
        return NULL;
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
        return NULL;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    ESP_LOGI(TAG, "Reading boot cart config");
    if (!spark_cart_load_boot_path(app->cart_path, sizeof(app->cart_path), err_detail, sizeof(err_detail))) {
        ESP_LOGE(TAG, "No cartridge configured; add %s/%s (%s)",
                 SPARK_FILESYSTEM_BASE_PATH, SPARK_BOOT_CONFIG_FILE, err_detail);
        spark_gui_draw_error_screen("NO CART CONFIG", err_detail);
        return NULL;
    }
    ESP_LOGI(TAG, "Boot cart path: %s", app->cart_path);

    memset(err_detail, 0, sizeof(err_detail));
    ESP_LOGI(TAG, "Loading cart wasm into PSRAM");
    if (!spark_cart_load_wasm_psram(app->cart_path, &app->cart, err_detail, sizeof(err_detail))) {
        ESP_LOGE(TAG, "Failed to load cartridge: %s (%s)", app->cart_path, err_detail);
        spark_gui_draw_error_screen("CART LOAD FAIL", err_detail[0] ? err_detail : app->cart_path);
        return NULL;
    }
    ESP_LOGI(TAG, "Cart loaded (wasm=%" PRIu32 " bytes, file=%" PRIu32 " bytes)",
             app->cart.wasm_size, app->cart.file_size);
    ESP_LOGI(TAG, "Cart header: stack=%" PRIu32 " bytes, static=%" PRIu32 " bytes",
             app->cart.stack_size, app->cart.static_size);
    {
        multi_heap_info_t info_spiram = {0};
        uint32_t budget = 8u * 1024u * 1024u;
        uint32_t available = app->cart.wasm_size >= budget ? 0 : (budget - app->cart.wasm_size);

        heap_caps_get_info(&info_spiram, MALLOC_CAP_SPIRAM);
        if (info_spiram.largest_free_block < available) {
            available = (uint32_t)info_spiram.largest_free_block;
        }

        ESP_LOGI(TAG, "WASM PSRAM budget: %" PRIu32 " bytes (available=%" PRIu32 " bytes, largest=%u)",
                 budget, available, (unsigned)info_spiram.largest_free_block);
    }

    spark_log_memory_snapshot("before wasm init");
    esp_rom_printf("[boot] runtime load start\n");
    {
        multi_heap_info_t info_spiram = {0};
        uint32_t header_size = app->cart.data_size - app->cart.wasm_size;
        uint32_t budget = 8u * 1024u * 1024u;
        uint32_t total_memory = app->cart.wasm_size >= budget ? 0 : (budget - app->cart.wasm_size);
        const uint8_t *wasm_data = app->cart.data + header_size;
        uint32_t min_pages = 0;
        uint32_t max_pages = 0;
        uint32_t mem_flags = 0;
        uint32_t mem_count = 0;
        uint32_t initial_memory = spark_wasm_find_initial_memory(wasm_data,
                                                                 app->cart.wasm_size,
                                                                 &min_pages,
                                                                 &max_pages,
                                                                 &mem_flags,
                                                                 &mem_count);
        uint32_t max_total = 0;

        heap_caps_get_info(&info_spiram, MALLOC_CAP_SPIRAM);
        if (info_spiram.largest_free_block < total_memory) {
            total_memory = (uint32_t)info_spiram.largest_free_block;
        }

        if (initial_memory > app->cart.stack_size) {
            max_total = initial_memory - app->cart.stack_size;
            if (max_total < total_memory) {
                total_memory = max_total;
            }
        }

        ESP_LOGI(TAG, "WASM memory: count=%" PRIu32 " flags=0x%" PRIx32 " min_pages=%" PRIu32
                      " max_pages=%" PRIu32 " initial_bytes=%" PRIu32,
                 mem_count, mem_flags, min_pages, max_pages, initial_memory);
        ESP_LOGI(TAG, "WASM memory budget: stack=%" PRIu32 " total=%" PRIu32
                      " largest_psram=%u free_psram=%u",
                 app->cart.stack_size, total_memory,
                 (unsigned)info_spiram.largest_free_block,
                 (unsigned)info_spiram.total_free_bytes);

        WamrAllocator allocator = {
            .malloc_func = spark_wamr_psram_malloc,
            .realloc_func = spark_wamr_psram_realloc,
            .free_func = spark_wamr_psram_free,
        };

        error = spark_runtime_load_wasm_ex(&app->runtime,
                                           wasm_data,
                                           app->cart.wasm_size,
                                           app->cart.stack_size,
                                           app->cart.static_offset,
                                           app->cart.static_size,
                                           total_memory,
                                           &allocator);
    }
    if (error) {
        esp_rom_printf("Runtime init failed: %s\n", error);
        spark_gui_draw_error_screen("RUNTIME INIT FAIL", error);
        spark_cart_unload(&app->cart);
        return NULL;
    }
    esp_rom_printf("[boot] runtime load done\n");
    esp_rom_printf("[boot] cart wasm=%" PRIu32 " static_off=%" PRIu32 " static_size=%" PRIu32 "\n",
                   app->runtime.cart.wasm_size,
                   app->runtime.engine.static_memory.offset,
                   app->runtime.engine.static_memory.size);

    app->reader.data = NULL;
    app->reader.size = app->cart.file_size;
    app->reader.static_offset = app->runtime.engine.static_memory.offset;
    app->reader.static_size = app->runtime.engine.static_memory.size;
    app->reader.file = app->cart.file;
    spark_engine_set_static_reader(&app->runtime.engine, spark_read_static_memory, &app->reader);

    esp_rom_printf("[boot] starting runtime\n");
    error = spark_engine_run_game(&app->runtime.engine, &app->runtime.wasm);
    if (error) {
        esp_rom_printf("Runtime start failed: %s\n", error);
        spark_gui_draw_error_screen("RUNTIME START FAIL", error);
        spark_runtime_deinit(&app->runtime);
        spark_cart_unload(&app->cart);
        return NULL;
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
                app->line_buffer[x] = spark_color_swap_bytes(color);
            }
            spark_device_draw_bitmap(0, y, SCREEN_WIDTH, y + 1, app->line_buffer);
        }

        app->frame_counter++;
        // esp_rom_delay_us(16000);
    }

    spark_runtime_deinit(&app->runtime);
    spark_cart_unload(&app->cart);
    return NULL;
}

void app_main(void)
{
    pthread_t thread;
    pthread_attr_t attr;
    int rc = 0;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 8192);

    rc = pthread_create(&thread, &attr, spark_app_thread, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "pthread_create failed: %d", rc);
        return;
    }

    pthread_join(thread, NULL);
}
