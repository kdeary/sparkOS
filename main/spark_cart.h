#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SPARK_FILESYSTEM_BASE_PATH "/carts"
#define SPARK_BOOT_CONFIG_FILE ".spark"

typedef struct SparkCartImage {
    uint8_t *data;
    uint32_t data_size;
    uint32_t file_size;
    uint32_t wasm_size;
    uint32_t stack_size;
    uint32_t static_offset;
    uint32_t static_size;
    FILE *file;
} SparkCartImage;

bool spark_cart_load_boot_path(char *out, size_t out_size, char *err, size_t err_size);
bool spark_cart_load_wasm_psram(const char *path,
                                SparkCartImage *out,
                                char *err,
                                size_t err_size);
void spark_cart_unload(SparkCartImage *cart);
