#include "ui.h"
#include "lcd.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[LCD_HEIGHT][LCD_WIDTH];
static unsigned fills;
static unsigned clears;

void lcd_init(void) { }
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    assert((unsigned)x + w <= LCD_WIDTH);
    assert((unsigned)y + h <= LCD_HEIGHT);
    ++fills;
    for(unsigned row = y; row < (unsigned)y + h; ++row)
        for(unsigned col = x; col < (unsigned)x + w; ++col)
            pixels[row][col] = color;
}
void lcd_clear(uint16_t color)
{
    ++clears;
    lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

static void snapshot(const char *directory, const char *name)
{
    if(!directory)
        return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%u-%s.ppm", directory, LCD_WIDTH, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%u %u\n255\n", LCD_WIDTH, LCD_HEIGHT);
    for(unsigned y = 0; y < LCD_HEIGHT; ++y)
        for(unsigned x = 0; x < LCD_WIDTH; ++x)
        {
            uint16_t p = pixels[y][x];
            fputc(((p >> 11) & 31U) * 255U / 31U, file);
            fputc(((p >> 5) & 63U) * 255U / 63U, file);
            fputc((p & 31U) * 255U / 31U, file);
        }
    fclose(file);
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : NULL;
    uint8_t header[] = {0U, 0x10U, 0x27U, 0U, 0U}; /* raw=10000 */
    uint8_t invalid[] = {0U, 0U, 0U, 0U, 0U};
    ui_init();
    snapshot(directory, "waiting");
    assert(!ui_validate_write(0U, header, 4U));
    assert(!ui_validate_write(0U, invalid, 5U));
    assert(ui_validate_write(0U, header, 5U));
    ui_initializing();
    snapshot(directory, "initializing");
    ui_flash_ready();
    ui_erasing(0U, 0U);
    ui_erasing(1U, 2U);
    snapshot(directory, "erasing");
    ui_write_complete(8192U, header, 1845U); /* hole: still zero */
    assert(ui_percent() == 0U);
    snapshot(directory, "zero");
    ui_write_complete(0U, header, 4096U);
    assert(ui_wire_size() == 10037U);
    assert(ui_percent() == 40U);
    snapshot(directory, "flashing");
    unsigned old_fills = fills;
    ui_write_complete(0U, header, 4096U);
    assert(ui_percent() == 40U && fills == old_fills);
    ui_write_complete(4096U, header, 4096U);
    assert(ui_percent() == 100U);
    assert(pixels[150][24] == 0x07E0U);
    assert(pixels[150][LCD_WIDTH - 25U] == 0x07E0U);
    snapshot(directory, "hundred");
    ui_write_complete(0x10000U, header, 4096U);
    snapshot(directory, "finalizing");
    ui_done();
    snapshot(directory, "done");
    ui_error("FLASH WRITE");
    snapshot(directory, "error");
    assert(clears == 1U);
    puts("UI_OK");
    return 0;
}
