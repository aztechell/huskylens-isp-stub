/* Copyright 2018 Canaan Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Polling DMA0 subset derived from Kendryte SDK dmac.c, commit
 * 02576ba67e8797444f3ee3f34c625b5ed048e707. No interrupts or allocation.
 * Reset is bounded; channel setup never calls the SDK's unbounded waits.
 */
#include "isp_dma.h"

#include <dmac.h>
#include <sleep.h>
#include <sysctl.h>

static volatile dmac_t *const lcd_dma = (volatile dmac_t *)DMAC_BASE_ADDR;

int isp_dma_init(uint64_t timeout_cycles)
{
    uint64_t start = read_cycle();
    sysctl_clock_enable(SYSCTL_CLOCK_DMA);
    lcd_dma->reset = 1U;
    while(lcd_dma->reset & 1U)
        if(read_cycle() - start >= timeout_cycles)
            return 0;
    lcd_dma->cfg = 0U;
    while(lcd_dma->cfg != 0U)
        if(read_cycle() - start >= timeout_cycles)
            return 0;
    lcd_dma->chen = 0x3F00U; /* disable all six channels, write-enable bits */
    lcd_dma->com_intclear = 0x11FU;
    lcd_dma->com_intsignal_en = 0U;
    for(unsigned channel = 0U; channel < 6U; ++channel)
    {
        lcd_dma->channel[channel].intstatus_en = 0U;
        lcd_dma->channel[channel].intsignal_en = 0U;
        lcd_dma->channel[channel].intclear = 0xFFFFFFFFU;
    }
    lcd_dma->cfg = 1U; /* DMA enabled, global interrupt disabled */
    sysctl_dma_select(SYSCTL_DMA_CHANNEL_0, SYSCTL_DMA_SELECT_SSI0_TX_REQ);
    return 1;
}

int isp_dma_busy(void)
{
    return (lcd_dma->chen & 1U) != 0U;
}

int isp_dma_start(const uint32_t *source, volatile uint32_t *destination, size_t count)
{
    dmac_ch_cfg_u_t cfg = {.data = 0U};
    dmac_ch_ctl_u_t ctl = {.data = 0U};
    if(isp_dma_busy() || !source || !destination || count == 0U || count > 0x400000U)
        return 0;
    cfg.ch_cfg.tt_fc = DMAC_MEM2PRF_DMA;
    cfg.ch_cfg.hs_sel_src = DMAC_HS_SOFTWARE;
    cfg.ch_cfg.hs_sel_dst = DMAC_HS_HARDWARE;
    lcd_dma->channel[0].cfg = cfg.data;
    lcd_dma->channel[0].sar = (uintptr_t)source;
    lcd_dma->channel[0].dar = (uintptr_t)destination;
    ctl.ch_ctl.sms = DMAC_MASTER1;
    ctl.ch_ctl.dms = DMAC_MASTER2;
    ctl.ch_ctl.sinc = DMAC_ADDR_INCREMENT;
    ctl.ch_ctl.dinc = DMAC_ADDR_NOCHANGE;
    ctl.ch_ctl.src_tr_width = DMAC_TRANS_WIDTH_32;
    ctl.ch_ctl.dst_tr_width = DMAC_TRANS_WIDTH_32;
    ctl.ch_ctl.src_msize = DMAC_MSIZE_4;
    ctl.ch_ctl.dst_msize = DMAC_MSIZE_4;
    lcd_dma->channel[0].ctl = ctl.data;
    lcd_dma->channel[0].block_ts = count - 1U;
    lcd_dma->channel[0].intclear = 0xFFFFFFFFU;
    lcd_dma->channel[0].intstatus_en = 0xFFFFFFFFU;
    __sync_synchronize();
    lcd_dma->chen = 0x101U;
    return 1;
}

int isp_dma_succeeded(void)
{
    uint64_t status = lcd_dma->channel[0].intstatus;
    return !isp_dma_busy() && (status & 2U) != 0U && (status & ~3ULL) == 0U;
}

void isp_dma_abort(void)
{
    lcd_dma->chen = 0x100U;
}
