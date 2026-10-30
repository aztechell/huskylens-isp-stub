#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dmac.h>
#include <sysctl.h>
#include <isp_dma.h>

unsigned char test_dma_storage[sizeof(dmac_t)] __attribute__((aligned(8)));
unsigned char test_spi_storage[8];
uint32_t test_gpio[8];
static volatile dmac_t *const regs = (volatile dmac_t *)test_dma_storage;
static uint64_t cycles;
static uint8_t stuck;
static uint8_t clear_reset;

uint64_t read_cycle(void)
{
    cycles += 10U;
    if(!stuck && clear_reset) regs->reset = 0U;
    return cycles;
}
int sysctl_clock_enable(int clock)
{ assert(clock == SYSCTL_CLOCK_DMA); clear_reset = 1U; return 0; }
int sysctl_dma_select(int channel, int selection)
{ assert(channel == SYSCTL_DMA_CHANNEL_0 && selection == SYSCTL_DMA_SELECT_SSI0_TX_REQ); return 0; }

int main(void)
{
    assert(isp_dma_init(500U));
    assert(regs->cfg == 1U && regs->com_intsignal_en == 0U);
    for(unsigned i = 0; i < 6U; ++i)
        assert(regs->channel[i].intsignal_en == 0U);
    uint32_t source[] = {0x1234U, 0xABCDU};
    uint32_t destination;
    regs->chen = 0U; /* write-enable bits are not stored by real hardware */
    assert(isp_dma_start(source, &destination, 2U));
    assert(regs->channel[0].sar == (uintptr_t)source);
    assert(regs->channel[0].dar == (uintptr_t)&destination);
    assert(regs->channel[0].block_ts == 1U);
    dmac_ch_cfg_u_t cfg = {.data = regs->channel[0].cfg};
    dmac_ch_ctl_u_t ctl = {.data = regs->channel[0].ctl};
    assert(cfg.ch_cfg.tt_fc == DMAC_MEM2PRF_DMA);
    assert(cfg.ch_cfg.hs_sel_src == DMAC_HS_SOFTWARE);
    assert(cfg.ch_cfg.hs_sel_dst == DMAC_HS_HARDWARE);
    assert(ctl.ch_ctl.src_tr_width == DMAC_TRANS_WIDTH_32);
    assert(ctl.ch_ctl.dst_tr_width == DMAC_TRANS_WIDTH_32);
    assert(ctl.ch_ctl.sinc == DMAC_ADDR_INCREMENT && ctl.ch_ctl.dinc == DMAC_ADDR_NOCHANGE);
    assert(isp_dma_busy() && !isp_dma_start(source, &destination, 1U));
    assert(!isp_dma_succeeded());
    regs->chen = 0U;
    const unsigned completions[] = {2U, 3U, 0xAU, 0x12U, 0x1BU};
    for(unsigned i = 0U; i < sizeof(completions) / sizeof(completions[0]); ++i)
    {
        regs->channel[0].intstatus = completions[i];
        assert(isp_dma_succeeded());
    }
    for(unsigned bit = 5U; bit <= 12U; ++bit)
    {
        regs->channel[0].intstatus = 2U | (1U << bit);
        assert(!isp_dma_succeeded());
    }
    regs->channel[0].intstatus = 0x10U;
    assert(!isp_dma_succeeded());
    assert(!isp_dma_start(source, &destination, 0U));
    isp_dma_abort(); assert(regs->chen == 0x100U);
    stuck = 1U;
    assert(!isp_dma_init(500U));
    puts("DMA_OK");
    return 0;
}
