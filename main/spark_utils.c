#include "spark_utils.h"

#include <ctype.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_rom_sys.h"

int spark_read_static_memory(uint32_t index, uint32_t size, uint8_t *out, void *userdata)
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

char *spark_trim(char *str)
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

const char *spark_path_basename(const char *path)
{
    if (!path) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

uint16_t spark_color_rgb_to_bgr(uint16_t rgb)
{
    uint16_t r = (rgb >> 11) & 0x1F;
    uint16_t g = (rgb >> 5) & 0x3F;
    uint16_t b = rgb & 0x1F;
    return (uint16_t)((b << 11) | (g << 5) | r);
}

uint16_t spark_color_swap_bytes(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

void spark_log_memory_snapshot(const char *tag)
{
    multi_heap_info_t info_8bit = {0};
    multi_heap_info_t info_internal = {0};
    multi_heap_info_t info_spiram = {0};

    heap_caps_get_info(&info_8bit, MALLOC_CAP_8BIT);
    heap_caps_get_info(&info_internal, MALLOC_CAP_INTERNAL);
    heap_caps_get_info(&info_spiram, MALLOC_CAP_SPIRAM);

    esp_rom_printf("\n[memory] %s\n", tag ? tag : "");
    esp_rom_printf("  free 8bit: %u bytes\n", (unsigned)info_8bit.total_free_bytes);
    esp_rom_printf("  largest 8bit: %u bytes\n", (unsigned)info_8bit.largest_free_block);
    esp_rom_printf("  free internal: %u bytes\n", (unsigned)info_internal.total_free_bytes);
    esp_rom_printf("  largest internal: %u bytes\n", (unsigned)info_internal.largest_free_block);
    esp_rom_printf("  free spiram: %u bytes\n", (unsigned)info_spiram.total_free_bytes);
    esp_rom_printf("  largest spiram: %u bytes\n", (unsigned)info_spiram.largest_free_block);
}
