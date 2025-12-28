#include "spark_cart.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

#include "parser/sprk_parser.h"

#include "spark_device.h"
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

const uint8_t *spark_cart_load_to_partition(const char *path,
                                            size_t *out_size,
                                            esp_partition_mmap_handle_t *out_handle,
                                            char *err,
                                            size_t err_size)
{
    const esp_partition_t *part = NULL;
    FILE *file = NULL;
    size_t size = 0;
    long length = 0;
    uint8_t *buffer = s_cart_io_buf;
    size_t offset = 0;
    esp_partition_mmap_handle_t mmap_handle = 0;
    const void *mmap_ptr = NULL;
    bool erased = false;
    uint32_t crc_sd = 0;
    uint32_t crc_flash = 0;

    if (!path) {
        if (err && err_size) {
            snprintf(err, err_size, "bad path");
        }
        return NULL;
    }

    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "rom");
    if (!part) {
        if (err && err_size) {
            snprintf(err, err_size, "rom partition missing");
        }
        return NULL;
    }

    ESP_LOGI(TAG, "Using ROM partition at 0x%08" PRIx32 " size=%" PRIu32,
             part->address, part->size);

    file = fopen(path, "rb");
    if (!file) {
        if (err && err_size) {
            snprintf(err, err_size, "open: %s", strerror(errno));
        }
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        if (err && err_size) {
            snprintf(err, err_size, "seek end: %s", strerror(errno));
        }
        fclose(file);
        return NULL;
    }

    length = ftell(file);
    if (length <= 0) {
        if (err && err_size) {
            snprintf(err, err_size, "size invalid");
        }
        fclose(file);
        return NULL;
    }
    size = (size_t)length;

    if ((size + 1) > part->size) {
        if (err && err_size) {
            snprintf(err, err_size, "cart too big (%u)", (unsigned)size);
        }
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        if (err && err_size) {
            snprintf(err, err_size, "seek start: %s", strerror(errno));
        }
        fclose(file);
        return NULL;
    }

    const char *filename = spark_path_basename(path);
    spark_gui_draw_progress_screen(filename, 0);

    while (offset < size) {
        size_t to_read = size - offset;
        if (to_read > sizeof(s_cart_io_buf)) {
            to_read = sizeof(s_cart_io_buf);
        }
        size_t got = fread(buffer, 1, to_read, file);
        if (got != to_read) {
            if (err && err_size) {
                snprintf(err, err_size, "read: %s", strerror(errno));
            }
            fclose(file);
            return NULL;
        }

        crc_sd = esp_rom_crc32_le(crc_sd, buffer, (uint32_t)got);

        uint32_t percent = (uint32_t)(((offset + got) * 100ULL) / size);
        spark_gui_draw_progress_screen(filename, percent);

        fclose(file);
        spark_device_unmount_filesystem();

        if (!erased) {
            size_t erase_size = (size + 1 + 0xFFF) & ~0xFFF;
            esp_err_t err_rc = esp_partition_erase_range(part, 0, erase_size);
            if (err_rc != ESP_OK) {
                if (err && err_size) {
                    snprintf(err, err_size, "erase: %s", esp_err_to_name(err_rc));
                }
                return NULL;
            }
            erased = true;
        }

        esp_err_t err_rc = esp_partition_write(part, offset, buffer, got);
        if (err_rc != ESP_OK) {
            if (err && err_size) {
                snprintf(err, err_size, "write: %s", esp_err_to_name(err_rc));
            }
            return NULL;
        }

        if (spark_device_mount_filesystem() != ESP_OK) {
            if (err && err_size) {
                snprintf(err, err_size, "remount failed");
            }
            return NULL;
        }
        file = fopen(path, "rb");
        if (!file) {
            if (err && err_size) {
                snprintf(err, err_size, "reopen: %s", strerror(errno));
            }
            return NULL;
        }
        if (fseek(file, (long)(offset + got), SEEK_SET) != 0) {
            if (err && err_size) {
                snprintf(err, err_size, "reseek: %s", strerror(errno));
            }
            fclose(file);
            return NULL;
        }
        offset += got;
    }

    fclose(file);
    spark_device_unmount_filesystem();

    uint8_t terminator = 0;
    esp_err_t err_rc = esp_partition_write(part, size, &terminator, 1);
    if (err_rc != ESP_OK) {
        if (err && err_size) {
            snprintf(err, err_size, "write nul: %s", esp_err_to_name(err_rc));
        }
        return NULL;
    }

    err_rc = esp_partition_mmap(part, 0, size + 1, ESP_PARTITION_MMAP_DATA, &mmap_ptr, &mmap_handle);
    if (err_rc != ESP_OK) {
        if (err && err_size) {
            snprintf(err, err_size, "mmap: %s", esp_err_to_name(err_rc));
        }
        return NULL;
    }

    crc_flash = esp_rom_crc32_le(0, (const uint8_t *)mmap_ptr, (uint32_t)size);
    if (crc_flash != crc_sd) {
        if (err && err_size) {
            snprintf(err, err_size, "flash verify fail");
        }
        esp_partition_munmap(mmap_handle);
        return NULL;
    }

    if (spark_device_mount_filesystem() != ESP_OK) {
        if (err && err_size) {
            snprintf(err, err_size, "remount failed");
        }
    }

    if (out_size) {
        *out_size = size;
    }
    if (out_handle) {
        *out_handle = mmap_handle;
    }
    return (const uint8_t *)mmap_ptr;
}

bool spark_cart_validate_image(const uint8_t *data, size_t size, char *err, size_t err_size)
{
    const char *parse_error = NULL;
    SparkCartridge cart = spark_cartridge_parse(data, (uint32_t)size, &parse_error);

    if (parse_error) {
        if (err && err_size) {
            snprintf(err, err_size, "%s", parse_error);
        }
        return false;
    }

    if (cart.wasm_size < 4 || !cart.wasm_data) {
        if (err && err_size) {
            snprintf(err, err_size, "missing wasm");
        }
        return false;
    }

    if (!(cart.wasm_data[0] == 0x00 && cart.wasm_data[1] == 0x61 &&
          cart.wasm_data[2] == 0x73 && cart.wasm_data[3] == 0x6d)) {
        if (err && err_size) {
            snprintf(err, err_size, "bad wasm magic");
        }
        return false;
    }

    return true;
}
