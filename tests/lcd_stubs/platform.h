#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
#include <stdint.h>
extern unsigned char test_spi_storage[];
extern unsigned char test_dma_storage[];
extern uint32_t test_gpio[];
#define SPI0_BASE_ADDR ((uintptr_t)test_spi_storage)
#define GPIOHS_BASE_ADDR ((uintptr_t)test_gpio)
#define DMAC_BASE_ADDR ((uintptr_t)test_dma_storage)
#endif
