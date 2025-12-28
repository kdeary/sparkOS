#pragma once

#include <stdint.h>

void spark_gui_draw_error_screen(const char *line1, const char *line2);
void spark_gui_draw_progress_screen(const char *filename, uint32_t percent);
