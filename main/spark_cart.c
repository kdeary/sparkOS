#include "spark_cart.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "spark_gui.h"
#include "spark_utils.h"

#define TAG "spark_firmware"

static uint8_t s_cart_io_buf[32 * 1024];

bool spark_cart_load_boot_path(char *out, size_t out_size, char *err, size_t err_size)
{
    char cfg_path[256];
    FILE *file = NULL;
    char line[256];

    if (!out || out_size == 0) {
        if (err && err_size) {
            snprintf(err, err_size, "bad args");
        }
        return false;
    }

    snprintf(cfg_path, sizeof(cfg_path), "%s/%s", SPARK_FILESYSTEM_BASE_PATH, SPARK_BOOT_CONFIG_FILE);
    file = fopen(cfg_path, "r");
    if (!file) {
        if (err && err_size) {
            snprintf(err, err_size, "open %s: %s", cfg_path, strerror(errno));
        }
        ESP_LOGW(TAG, "Boot config not found: %s (%s)", cfg_path, strerror(errno));
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *entry = spark_trim(line);
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

        value = spark_trim((char *)value);
        if (!value[0]) {
            continue;
        }

        if (value[0] == '/') {
            snprintf(out, out_size, "%s", value);
        } else {
            snprintf(out, out_size, "%s/%s", SPARK_FILESYSTEM_BASE_PATH, value);
        }
        fclose(file);
        return true;
    }

    fclose(file);
    if (err && err_size) {
        snprintf(err, err_size, "missing CART_FILE");
    }
    return false;
}

static bool spark_cart_read_header(FILE *file,
                                   uint8_t *header,
                                   size_t header_size,
                                   uint32_t *wasm_size,
                                   uint32_t *stack_size,
                                   char *err,
                                   size_t err_size)
{
    static const uint8_t magic[] = { 'S','P','A','R','K','Y',0x04,0x06 };

    if (fread(header, 1, header_size, file) != header_size) {
        if (err && err_size) {
            snprintf(err, err_size, "header read failed");
        }
        return false;
    }

    if (memcmp(header, magic, sizeof(magic)) != 0) {
        if (err && err_size) {
            snprintf(err, err_size, "invalid .sprk magic");
        }
        return false;
    }

    memcpy(wasm_size, header + sizeof(magic), sizeof(uint32_t));
    memcpy(stack_size, header + sizeof(magic) + sizeof(uint32_t), sizeof(uint32_t));
    return true;
}

bool spark_cart_load_wasm_psram(const char *path,
                                SparkCartImage *out,
                                char *err,
                                size_t err_size)
{
    static const size_t header_size = 16;
    FILE *file = NULL;
    uint8_t header[16];
    long length = 0;
    size_t file_size = 0;
    uint32_t wasm_size = 0;
    uint32_t stack_size = 0;
    uint8_t *image = NULL;
    size_t image_size = 0;
    size_t offset = 0;

    if (!path || !out) {
        if (err && err_size) {
            snprintf(err, err_size, "bad args");
        }
        return false;
    }

    memset(out, 0, sizeof(*out));

    file = fopen(path, "rb");
    if (!file) {
        if (err && err_size) {
            snprintf(err, err_size, "open: %s", strerror(errno));
        }
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        if (err && err_size) {
            snprintf(err, err_size, "seek end: %s", strerror(errno));
        }
        fclose(file);
        return false;
    }

    length = ftell(file);
    if (length <= 0) {
        if (err && err_size) {
            snprintf(err, err_size, "size invalid");
        }
        fclose(file);
        return false;
    }
    file_size = (size_t)length;

    if (fseek(file, 0, SEEK_SET) != 0) {
        if (err && err_size) {
            snprintf(err, err_size, "seek start: %s", strerror(errno));
        }
        fclose(file);
        return false;
    }

    if (!spark_cart_read_header(file, header, header_size, &wasm_size, &stack_size, err, err_size)) {
        fclose(file);
        return false;
    }

    if (header_size + wasm_size > file_size) {
        if (err && err_size) {
            snprintf(err, err_size, "invalid .sprk wasm size");
        }
        fclose(file);
        return false;
    }

    if (wasm_size < 4) {
        if (err && err_size) {
            snprintf(err, err_size, "missing wasm");
        }
        fclose(file);
        return false;
    }

    image_size = header_size + wasm_size;
    image = (uint8_t *)heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image) {
        if (err && err_size) {
            snprintf(err, err_size, "psram alloc failed");
        }
        fclose(file);
        return false;
    }

    memcpy(image, header, header_size);

    const char *filename = spark_path_basename(path);
    spark_gui_draw_progress_screen(filename, 0);

    offset = 0;
    while (offset < wasm_size) {
        size_t to_read = wasm_size - offset;
        if (to_read > sizeof(s_cart_io_buf)) {
            to_read = sizeof(s_cart_io_buf);
        }
        if (fread(s_cart_io_buf, 1, to_read, file) != to_read) {
            if (err && err_size) {
                snprintf(err, err_size, "wasm read failed");
            }
            heap_caps_free(image);
            fclose(file);
            return false;
        }

        memcpy(image + header_size + offset, s_cart_io_buf, to_read);
        offset += to_read;

        uint32_t percent = (uint32_t)((offset * 100ULL) / wasm_size);
        spark_gui_draw_progress_screen(filename, percent);
    }

    if (!(image[header_size] == 0x00 && image[header_size + 1] == 0x61 &&
          image[header_size + 2] == 0x73 && image[header_size + 3] == 0x6d)) {
        if (err && err_size) {
            snprintf(err, err_size, "bad wasm magic");
        }
        heap_caps_free(image);
        fclose(file);
        return false;
    }

    out->data = image;
    out->data_size = (uint32_t)image_size;
    out->file_size = (uint32_t)file_size;
    out->wasm_size = wasm_size;
    out->stack_size = stack_size;
    out->static_offset = (uint32_t)(header_size + wasm_size);
    out->static_size = (uint32_t)(file_size - header_size - wasm_size);
    out->file = file;

    return true;
}

void spark_cart_unload(SparkCartImage *cart)
{
    if (!cart) {
        return;
    }

    if (cart->file) {
        fclose(cart->file);
        cart->file = NULL;
    }

    if (cart->data) {
        heap_caps_free(cart->data);
        cart->data = NULL;
    }

    memset(cart, 0, sizeof(*cart));
}
