#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

bool spark_device_init_display(void);
bool spark_device_display_ready(void);
void spark_device_draw_bitmap(int xs, int ys, int xe, int ye, const uint16_t *data);

esp_err_t spark_device_mount_filesystem(void);
void spark_device_unmount_filesystem(void);
