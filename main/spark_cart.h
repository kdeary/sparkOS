#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_partition.h"

#define SPARK_FILESYSTEM_BASE_PATH "/carts"
#define SPARK_BOOT_CONFIG_FILE ".spark"

bool spark_cart_load_boot_path(char *out, size_t out_size, char *err, size_t err_size);
const uint8_t *spark_cart_load_to_partition(const char *path,
                                            size_t *out_size,
                                            esp_partition_mmap_handle_t *out_handle,
                                            char *err,
                                            size_t err_size);
bool spark_cart_validate_image(const uint8_t *data, size_t size, char *err, size_t err_size);
