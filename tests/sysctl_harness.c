/* Run the real minimal BSP clock dispatcher: mocking it hid the DMA hang. */
#include <assert.h>
#include <stdio.h>
#include <sysctl.h>

unsigned char test_sysctl_storage[sizeof(sysctl_t)] __attribute__((aligned(8)));
int usleep(uint64_t microseconds) { (void)microseconds; return 0; }
int msleep(uint64_t milliseconds) { (void)milliseconds; return 0; }

int main(void)
{
    assert(sysctl_clock_enable(SYSCTL_CLOCK_UART3) == 0);
    assert(sysctl_clock_enable(SYSCTL_CLOCK_SPI3) == 0);
    assert(sysctl_clock_enable(SYSCTL_CLOCK_SPI0) == 0);
    assert(sysctl_clock_enable(SYSCTL_CLOCK_DMA) == 0);
    assert(sysctl->clk_en_peri.dma_clk_en == 1U);
    assert(sysctl->clk_en_peri.uart3_clk_en == 1U);
    assert(sysctl->clk_en_peri.spi3_clk_en == 1U);
    assert(sysctl->clk_en_peri.spi0_clk_en == 1U);
    assert(sysctl_clock_disable(SYSCTL_CLOCK_DMA) == 0);
    assert(sysctl->clk_en_peri.dma_clk_en == 0U);
    assert(sysctl->clk_en_peri.uart3_clk_en == 1U);
    assert(sysctl->clk_en_peri.spi3_clk_en == 1U);
    puts("SYSCTL_OK");
    return 0;
}
