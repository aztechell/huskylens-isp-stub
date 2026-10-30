/* Exercise the real LCD driver with fake DMA/MMIO, including failure paths. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include <spi.h>
#include <sysctl.h>
#include <isp_dma.h>

unsigned char test_spi_storage[sizeof(spi_t)] __attribute__((aligned(8)));
unsigned char test_dma_storage[8] __attribute__((aligned(8)));
uint32_t test_gpio[8];
static volatile spi_t *const regs = (volatile spi_t *)test_spi_storage;
static unsigned transfers;
static unsigned aborted;
static unsigned busy_calls;
static unsigned route[48];
static unsigned bank_mask;
static unsigned clock_enabled;
static uint64_t cycles;
static uint32_t last_words[LCD_WIDTH * LCD_HEIGHT];
static size_t last_count;
static uint32_t last_ctrl;
static uint32_t last_phase;
static unsigned command_count;
static uint8_t commands[32];
static uint8_t init_ok = 1U;
static uint8_t force_busy;
static uint8_t stall;
static uint8_t transfer_ok = 1U;

uint64_t read_cycle(void) { cycles += 10U; return cycles; }
int msleep(uint64_t milliseconds) { (void)milliseconds; return 0; }
uint32_t sysctl_clock_get_freq(int clock)
{ return clock == SYSCTL_CLOCK_CPU ? 1000U : 120000000U; }
int sysctl_clock_enable(int clock) { clock_enabled |= 1U << clock; return 0; }
int sysctl_clock_set_threshold(int clock, int value)
{ (void)clock; (void)value; return 0; }
int sysctl_set_power_mode(int bank, int voltage)
{ assert(voltage == SYSCTL_POWER_V18); bank_mask |= 1U << bank; return 0; }
int sysctl_set_spi0_dvp_data(int enable) { assert(enable == 1); return 0; }
int fpioa_set_function(int pin, int function)
{ route[pin] = (unsigned)function + 1U; return 0; }
int isp_dma_init(uint64_t timeout)
{ assert(timeout == 500U); return init_ok; }
int isp_dma_busy(void)
{
    if(force_busy) return 1;
    if(busy_calls)
    {
        if(!stall) --busy_calls;
        return 1;
    }
    return 0;
}
int isp_dma_succeeded(void) { return transfer_ok; }
void isp_dma_abort(void) { ++aborted; }
int isp_dma_start(const uint32_t *src, volatile uint32_t *dst, size_t count)
{
    assert(regs->ser == 0U); /* DMA must precede chip select */
    assert(regs->ssienr == 1U && regs->dmacr == 2U);
    assert(dst == &regs->dr[0]);
    assert(count <= LCD_WIDTH * LCD_HEIGHT);
    memcpy(last_words, src, count * sizeof(*src));
    last_count = count;
    last_ctrl = regs->ctrlr0;
    last_phase = regs->spi_ctrlr0;
    if((test_gpio[3] & (1U << GPIO_LCD_DC)) == 0U)
    {
        assert(count == 1U);
        assert(last_phase == (2U | (2U << 8)));
        if(command_count < sizeof(commands)) commands[command_count++] = (uint8_t)src[0];
    }
    ++transfers;
    busy_calls = 1U;
    return 1;
}

static void reset(void)
{
    memset(test_spi_storage, 0, sizeof(test_spi_storage));
    regs->sr = 4U;
    transfers = aborted = busy_calls = command_count = 0U;
    cycles = 0U;
    init_ok = transfer_ok = 1U;
    force_busy = stall = 0U;
}

int main(void)
{
    reset();
    lcd_init();
    assert(bank_mask == ((1U << SYSCTL_POWER_BANK6) | (1U << SYSCTL_POWER_BANK7)));
    assert(route[17] && route[36] && route[37] && route[38] && route[39]);
    assert(!route[18] && !route[19] && !route[20] && !route[21] && !route[22] && !route[24]);
    assert((test_gpio[3] & (1U << GPIO_LCD_BACKLIGHT)) == 0U);
    assert(regs->baudr == 8U);
    const uint8_t expected[] = {0x01, 0x11, 0x3A, 0x36, 0x21, 0x13, 0x29};
    assert(command_count == sizeof(expected));
    assert(memcmp(commands, expected, sizeof(expected)) == 0);
    lcd_fill_rect(4U, 5U, 3U, 2U, 0xF81FU);
    assert(last_count == 6U);
    assert(last_ctrl == ((15U << 16) | (3U << 21) | (1U << 8)));
    assert(last_phase == (2U | (4U << 2)));
    for(unsigned i = 0; i < 6U; ++i) assert(last_words[i] == 0xF81FU);
    assert(regs->ser == 0U && regs->ssienr == 0U && regs->dmacr == 0U);
    lcd_clear(0x1234U);
    assert(last_count == LCD_WIDTH * LCD_HEIGHT);
    assert(last_words[last_count - 1U] == 0x1234U);
    lcd_fill_rect(239U, 239U, 100U, 100U, 0xABCDU);
    assert(last_count == 1U && last_words[0] == 0xABCDU);

    /* A DMA timeout disables subsequent drawing and releases SPI. */
    stall = 1U;
    lcd_clear(0U);
    assert(aborted == 1U);
    unsigned old_transfers = transfers;
    lcd_clear(0U);
    assert(transfers == old_transfers && regs->dmacr == 0U && regs->ser == 0U);

    reset(); lcd_init();
    /* Do not reuse the staging buffer while a channel is still busy. */
    uint32_t original = last_words[0];
    force_busy = 1U;
    old_transfers = transfers;
    lcd_clear(0xFFFFU);
    assert(transfers == old_transfers && last_words[0] == original && aborted == 1U);

    reset(); init_ok = 0U; lcd_init(); lcd_clear(0U);
    assert(transfers == 0U && aborted == 1U);

    reset(); lcd_init(); transfer_ok = 0U; lcd_clear(0U);
    assert(aborted == 1U);

    reset(); lcd_init(); regs->sr = 0U; lcd_clear(0U);
    assert(aborted == 1U && regs->ssienr == 0U);
    puts("LCD_OK");
    return 0;
}
