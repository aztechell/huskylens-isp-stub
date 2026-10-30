#ifndef ISP_DMA_H
#define ISP_DMA_H

#include <stddef.h>
#include <stdint.h>

/* Polling-only DMA0 for the Cube LCD, using uncached 32-bit FIFO entries. */
int isp_dma_init(uint64_t timeout_cycles);
int isp_dma_start(const uint32_t *source, volatile uint32_t *destination, size_t count);
int isp_dma_busy(void);
int isp_dma_succeeded(void);
void isp_dma_abort(void);

#endif
