#include "spark_gui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "engine/spark_engine.h"

#include "spark_config.h"
#include "spark_device.h"
#include "spark_utils.h"

typedef struct Glyph {
    char ch;
    uint8_t rows[7];
} Glyph;

static const Glyph FONT_5X7[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'-', {0, 0, 0b11110, 0, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 0, 0b00100}},
    {':', {0, 0b00100, 0, 0, 0, 0b00100, 0}},
    {'/', {0b00001, 0b00010, 0b00100, 0b00100, 0b01000, 0b01000, 0b10000}},
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b01110, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b10001, 0b01110}},
    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'C', {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {'D', {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}},
    {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'G', {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110}},
    {'H', {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'I', {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'J', {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},
    {'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N', {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}},
    {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'Q', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10010, 0b01101}},
    {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S', {0b01110, 0b10001, 0b10000, 0b01110, 0b00001, 0b10001, 0b01110}},
    {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}},
    {'X', {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
    {'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
    {'%', {0b11001, 0b11010, 0b00100, 0b01000, 0b10110, 0b00110, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 0b11111}},
};

static uint16_t s_scanline[SCREEN_WIDTH];

static const Glyph *find_glyph(char ch)
{
    size_t count = sizeof(FONT_5X7) / sizeof(FONT_5X7[0]);
    for (size_t i = 0; i < count; ++i) {
        if (FONT_5X7[i].ch == ch) {
            return &FONT_5X7[i];
        }
    }
    return &FONT_5X7[0];
}

static void draw_text_line(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    if (!text || !spark_device_display_ready()) {
        return;
    }

    if (scale < 1) {
        scale = 1;
    }

    int cursor_x = x;
    uint16_t fg_out = spark_color_swap_bytes(fg);
    uint16_t bg_out = spark_color_swap_bytes(bg);

    while (*text) {
        char ch = *text++;
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 32);
        }
        const Glyph *glyph = find_glyph(ch);
        int glyph_w = 5 * scale;
        int glyph_h = 7 * scale;
        uint16_t pixels[5 * 7 * 4];

        if ((size_t)(glyph_w * glyph_h) > sizeof(pixels) / sizeof(pixels[0])) {
            return;
        }

        int idx = 0;
        for (int row = 0; row < 7; ++row) {
            for (int sy = 0; sy < scale; ++sy) {
                for (int col = 0; col < 5; ++col) {
                    uint16_t color = (glyph->rows[row] & (1 << (4 - col))) ? fg_out : bg_out;
                    for (int sx = 0; sx < scale; ++sx) {
                        pixels[idx++] = color;
                    }
                }
            }
        }

        spark_device_draw_bitmap(cursor_x, y, cursor_x + glyph_w, y + glyph_h, pixels);
        cursor_x += glyph_w + scale;
    }
}

void spark_gui_draw_error_screen(const char *line1, const char *line2)
{
    if (!spark_device_display_ready()) {
        return;
    }

    uint16_t *line = s_scanline;
    uint16_t bg = 0x0000;
    uint16_t fg = 0xFFFF;
    uint16_t bg_out = spark_color_swap_bytes(bg);
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        line[x] = bg_out;
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        spark_device_draw_bitmap(0, y, SCREEN_WIDTH, y + 1, line);
    }

    draw_text_line(10, 10, "SPARK ERROR", fg, bg, 2);
    if (line1) {
        draw_text_line(10, 30, line1, fg, bg, 1);
    }
    if (line2) {
        draw_text_line(10, 45, line2, fg, bg, 1);
    }
}

void spark_gui_draw_progress_screen(const char *filename, uint32_t percent)
{
    if (!spark_device_display_ready()) {
        return;
    }

    uint16_t *line = s_scanline;
    uint16_t bg = 0x0000;
    uint16_t fg = 0xFFFF;
    uint16_t border = 0xFFFF;
    uint16_t fill = 0x07E0;
    uint16_t bg_out = spark_color_swap_bytes(bg);
    uint16_t border_out = spark_color_swap_bytes(border);
    uint16_t fill_out = spark_color_swap_bytes(fill);

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        line[x] = bg_out;
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        spark_device_draw_bitmap(0, y, SCREEN_WIDTH, y + 1, line);
    }

    int bar_w = (SCREEN_WIDTH * 3) / 4;
    int bar_h = 16;
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = (SCREEN_HEIGHT - bar_h) / 2;

    if (filename && filename[0]) {
        int text_w = (int)strlen(filename) * 6;
        int text_x = (SCREEN_WIDTH - text_w) / 2;
        if (text_x < 0) {
            text_x = 0;
        }
        draw_text_line(text_x, bar_y - 24, filename, fg, bg, 1);
    }

    for (int y = 0; y < bar_h; ++y) {
        for (int x = 0; x < bar_w; ++x) {
            if (y == 0 || y == bar_h - 1 || x == 0 || x == bar_w - 1) {
                line[x] = border_out;
            } else {
                line[x] = bg_out;
            }
        }
        spark_device_draw_bitmap(bar_x, bar_y + y, bar_x + bar_w, bar_y + y + 1, line);
    }

    uint32_t clamped = percent > 100 ? 100 : percent;
    int fill_w = (int)((bar_w - 2) * clamped / 100);
    if (fill_w < 0) {
        fill_w = 0;
    }
    if (fill_w > bar_w - 2) {
        fill_w = bar_w - 2;
    }

    for (int y = 1; y < bar_h - 1; ++y) {
        for (int x = 0; x < bar_w - 2; ++x) {
            line[x] = (x < fill_w) ? fill_out : bg_out;
        }
        spark_device_draw_bitmap(bar_x + 1, bar_y + y, bar_x + bar_w - 1, bar_y + y + 1, line);
    }

    char pct[16];
    snprintf(pct, sizeof(pct), "%" PRIu32 "%%", clamped);
    draw_text_line((SCREEN_WIDTH - (int)strlen(pct) * 6) / 2, bar_y + bar_h + 12, pct, fg, bg, 1);
}
