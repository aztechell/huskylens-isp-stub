#include <stdint.h>
enum { SYSCTL_CLOCK_CPU, SYSCTL_CLOCK_SPI0, SYSCTL_CLOCK_DMA,
       SYSCTL_POWER_BANK6, SYSCTL_POWER_BANK7, SYSCTL_POWER_V18,
       SYSCTL_THRESHOLD_SPI0, SYSCTL_DMA_CHANNEL_0, SYSCTL_DMA_SELECT_SSI0_TX_REQ };
uint32_t sysctl_clock_get_freq(int clock);
int sysctl_clock_enable(int clock);
int sysctl_clock_set_threshold(int clock, int value);
int sysctl_set_power_mode(int bank, int voltage);
int sysctl_set_spi0_dvp_data(int enable);
int sysctl_dma_select(int channel, int selection);
