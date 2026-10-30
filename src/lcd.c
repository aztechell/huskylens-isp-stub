#include "lcd.h"

#include <fpioa.h>
#include <platform.h>
#include <sleep.h>
#include <spi.h>
#include <sysctl.h>

#define LCD_SPI SPI_DEVICE_0
#define LCD_CS SPI_CHIP_SELECT_3

#if ISP_BOARD_CUBE
#include <isp_dma.h>
static uint32_t dma_words[LCD_WIDTH * LCD_HEIGHT]
    __attribute__((aligned(64), section(".bss")));
#else
static uint8_t line_buffer[LCD_WIDTH * 2U];
#endif
static uint8_t lcd_available = 1U;
static volatile spi_t *const lcd_spi = (volatile spi_t *)SPI0_BASE_ADDR;
static volatile uint32_t *const gpiohs = (volatile uint32_t *)GPIOHS_BASE_ADDR;

static void gpio_write(uint8_t pin, uint8_t high)
{
    uint32_t mask = 1U << pin;
    gpiohs[1] &= ~mask; /* input_en */
    gpiohs[2] |= mask;  /* output_en */
    if(high) gpiohs[3] |= mask;
    else gpiohs[3] &= ~mask;
}

#if ISP_BOARD_CUBE
static uint32_t *uncached_words(void)
{
    uintptr_t address = (uintptr_t)dma_words;
    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (uint32_t *)address;
}

static uint64_t lcd_timeout_cycles(void)
{
    return sysctl_clock_get_freq(SYSCTL_CLOCK_CPU) / 2U; /* 500 ms */
}

static void cube_disable(void)
{
    lcd_spi->dmacr = 0U;
    isp_dma_abort();
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
    lcd_available = 0U;
}

static uint8_t cube_can_stage(void)
{
    if(!lcd_available)
        return 0U;
    if(isp_dma_busy())
    {
        cube_disable();
        return 0U;
    }
    return 1U;
}

static void cube_send_words(size_t count, uint32_t frame_bits, uint8_t command)
{
    uint64_t start = read_cycle();
    uint64_t timeout = lcd_timeout_cycles();
    if(!lcd_available)
        return;
    lcd_spi->ssienr = 0U;
    lcd_spi->ser = 0U;
    lcd_spi->ctrlr0 = ((frame_bits - 1U) << 16) | (3U << 21) | (1U << 8);
    /* OCTAL instructions use 8 bits; data uses an 8/16-bit address phase. */
    lcd_spi->spi_ctrlr0 = 2U | (command ? 2U << 8 : (frame_bits / 4U) << 2);
    lcd_spi->ssienr = 1U;
    lcd_spi->dmacr = 2U;
    if(!isp_dma_start(uncached_words(), &lcd_spi->dr[0], count))
        goto failed;
    /* Arm DMA before selecting the panel: no CPU gaps inside a transaction. */
    lcd_spi->ser = 1U << LCD_CS;
    while(isp_dma_busy())
        if(read_cycle() - start >= timeout)
            goto failed;
    if(!isp_dma_succeeded())
        goto failed;
    while((lcd_spi->sr & 0x05U) != 0x04U)
        if(read_cycle() - start >= timeout)
            goto failed;
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
    lcd_spi->dmacr = 0U;
    return;
failed:
    cube_disable();
}

static void cube_send_bytes(const uint8_t *data, size_t length, uint8_t command)
{
    uint32_t *words = uncached_words();
    if(!cube_can_stage())
        return;
    for(size_t i = 0U; i < length; ++i)
        words[i] = data[i];
    cube_send_words(length, 8U, command);
}
#else
static void spi_send_bytes(const uint8_t *data, size_t length)
{
    size_t offset = 0U;
    uint32_t watchdog = 0U;
    if(!lcd_available)
        return;
    lcd_spi->ssienr = 0U;
    lcd_spi->ctrlr0 = (7U << 16) | (1U << 8); /* 8-bit, transmit-only */
    lcd_spi->ssienr = 1U;
    lcd_spi->ser = 1U << LCD_CS;
    while(offset < length)
    {
        size_t available = 32U - lcd_spi->txflr;
        if(available > length - offset) available = length - offset;
        if(available == 0U)
        {
            if(++watchdog >= 1000000U)
            {
                lcd_available = 0U;
                lcd_spi->ser = 0U;
                lcd_spi->ssienr = 0U;
                return;
            }
            continue;
        }
        watchdog = 0U;
        while(available--) lcd_spi->dr[0] = data[offset++];
    }
    watchdog = 0U;
    while((lcd_spi->sr & 0x05U) != 0x04U)
    {
        if(++watchdog >= 1000000U)
        {
            lcd_available = 0U;
            break;
        }
    }
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
}
#endif

static void lcd_send(uint8_t data_mode, const uint8_t *data, size_t length)
{
    gpio_write(GPIO_LCD_DC, data_mode);
#if ISP_BOARD_CUBE
    cube_send_bytes(data, length, !data_mode);
#else
    spi_send_bytes(data, length);
#endif
}

static void lcd_command(uint8_t command)
{
    lcd_send(0U, &command, 1U);
}

static void lcd_data_u8(uint8_t data)
{
    lcd_send(1U, &data, 1U);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    lcd_command(0x2A);
    data[0] = (uint8_t)(x0 >> 8); data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8); data[3] = (uint8_t)x1;
    lcd_send(1U, data, sizeof(data));
    lcd_command(0x2B);
    data[0] = (uint8_t)(y0 >> 8); data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8); data[3] = (uint8_t)y1;
    lcd_send(1U, data, sizeof(data));
    lcd_command(0x2C);
}

void lcd_init(void)
{
#if !ISP_BOARD_CUBE
    static const uint8_t b2[] = {0x01, 0x01, 0x00, 0x01, 0x01};
    static const uint8_t d0[] = {0xA4, 0xA1};
#endif

    lcd_available = 1U;
#if ISP_BOARD_CUBE
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
    gpio_write(GPIO_LCD_BACKLIGHT, 1U); /* active-low: route while off */
#endif
    fpioa_set_function(LCD_PIN_DC, FUNC_GPIOHS15);
    fpioa_set_function(LCD_PIN_CS, FUNC_SPI0_SS3);
    fpioa_set_function(LCD_PIN_CLOCK, FUNC_SPI0_SCLK);
#if !ISP_BOARD_CUBE
    fpioa_set_function(LCD_PIN_MOSI, FUNC_SPI0_D0);
#endif
    fpioa_set_function(LCD_PIN_RESET, FUNC_GPIOHS14);
    fpioa_set_function(LCD_PIN_BACKLIGHT, FUNC_GPIOHS13);
    gpio_write(GPIO_LCD_DC, 0U);
    gpio_write(GPIO_LCD_RESET, 1U);
#if !ISP_BOARD_CUBE
    gpio_write(GPIO_LCD_BACKLIGHT, LCD_BACKLIGHT_ON);
#endif

    sysctl_set_spi0_dvp_data(1U);
    sysctl_clock_enable(SYSCTL_CLOCK_SPI0);
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_SPI0, 0U);
    lcd_spi->baudr = sysctl_clock_get_freq(SYSCTL_CLOCK_SPI0) / LCD_CLOCK_HZ;
    if(lcd_spi->baudr < 2U) lcd_spi->baudr = 2U;
    lcd_spi->baudr = (lcd_spi->baudr + 1U) & ~1U;
    lcd_spi->imr = 0U;
    lcd_spi->dmacr = 0U;
    lcd_spi->dmatdlr = 0x10U;
    lcd_spi->dmardlr = 0U;
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
    lcd_spi->ctrlr0 = 7U << 16;
    lcd_spi->spi_ctrlr0 = 0U;
    lcd_spi->endian = 0U;

#if ISP_BOARD_CUBE
    if(!isp_dma_init(lcd_timeout_cycles()))
    {
        cube_disable();
        return;
    }
#endif
#ifdef ISP_LCD_INIT_ONLY
    return;
#endif

#if ISP_BOARD_CUBE
    gpio_write(GPIO_LCD_RESET, 0U); msleep(50U);
    gpio_write(GPIO_LCD_RESET, 1U); msleep(50U);
    lcd_command(0x01); msleep(50U);
    lcd_command(0x11); msleep(120U);
    lcd_command(0x3A); lcd_data_u8(0x55); msleep(10U);
    lcd_command(0x36); lcd_data_u8(0x60);
    lcd_command(0x21); msleep(10U);
    lcd_command(0x13); msleep(10U);
    lcd_command(0x29);
    if(lcd_available)
        gpio_write(GPIO_LCD_BACKLIGHT, LCD_BACKLIGHT_ON);
#else
    gpio_write(GPIO_LCD_RESET, 1U); msleep(1U);
    gpio_write(GPIO_LCD_RESET, 0U); msleep(1U);
    gpio_write(GPIO_LCD_RESET, 1U); msleep(125U);
    lcd_command(0x11); msleep(120U);
    lcd_command(0x36); lcd_data_u8(0xA0);
    lcd_command(0x3A); lcd_data_u8(0x05);
    lcd_command(0x21);
    lcd_command(0xB2); lcd_send(1U, b2, sizeof(b2));
    lcd_command(0xB7); lcd_data_u8(0x75);
    lcd_command(0xBB); lcd_data_u8(0x22);
    lcd_command(0xC0); lcd_data_u8(0x2C);
    lcd_command(0xC2); lcd_data_u8(0x01);
    lcd_command(0xC3); lcd_data_u8(0x13);
    lcd_command(0xC4); lcd_data_u8(0x20);
    lcd_command(0xC6); lcd_data_u8(0xE1);
    lcd_command(0xD0); lcd_send(1U, d0, sizeof(d0));
    lcd_command(0xD6); lcd_data_u8(0xA1);
    lcd_command(0x29); msleep(20U);
#endif
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color)
{
    if(!lcd_available || x >= LCD_WIDTH || y >= LCD_HEIGHT || width == 0U || height == 0U)
        return;
    if(x + width > LCD_WIDTH) width = LCD_WIDTH - x;
    if(y + height > LCD_HEIGHT) height = LCD_HEIGHT - y;
#if ISP_BOARD_CUBE
    lcd_window(x, y, x + width - 1U, y + height - 1U);
    if(!cube_can_stage())
        return;
    uint32_t *words = uncached_words();
    size_t count = (size_t)width * height;
    for(size_t i = 0U; i < count; ++i)
        words[i] = color;
    gpio_write(GPIO_LCD_DC, 1U);
    cube_send_words(count, 16U, 0U);
#else
    for(uint16_t i = 0; i < width; ++i)
    {
        line_buffer[i * 2U] = (uint8_t)(color >> 8);
        line_buffer[i * 2U + 1U] = (uint8_t)color;
    }
    lcd_window(x, y, x + width - 1U, y + height - 1U);
    gpio_write(GPIO_LCD_DC, 1U);
    for(uint16_t row = 0; row < height; ++row)
        spi_send_bytes(line_buffer, width * 2U);
#endif
}

void lcd_clear(uint16_t color)
{
    lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}
