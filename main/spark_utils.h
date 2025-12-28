#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SparkStaticReader {
    const uint8_t *data;
    uint32_t size;
    uint32_t static_offset;
    uint32_t static_size;
} SparkStaticReader;

int spark_read_static_memory(uint32_t index, uint32_t size, uint8_t *out, void *userdata);
char *spark_trim(char *str);
const char *spark_path_basename(const char *path);
uint16_t spark_color_rgb_to_bgr(uint16_t rgb);
uint16_t spark_color_swap_bytes(uint16_t color);
void spark_log_memory_snapshot(const char *tag);
