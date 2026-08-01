/*
 * HuskyLens display-aware K210 second-stage ISP.
 * Protocol and flash driver architecture derived from loboris/ktool,
 * licensed under Apache-2.0.
 */
#include "flash.h"
#include "isp_protocol.h"
#include "slip.h"
#include "ui.h"

#include <fpioa.h>
#include <platform.h>
#include <sleep.h>
#include <string.h>
#include <sysctl.h>
#include <uart.h>

#define UART_PORT UART_DEVICE_3
#define FLASH_CAPACITY (16U * 1024U * 1024U)
#define SECTOR_SIZE 4096U
#define BLOCK_SIZE 65536U

static uint8_t rx_buffer[ISP_RX_CAPACITY];
static slip_decoder_t decoder;
static uint8_t flash_initialized;

typedef struct {
    uint32_t start;
    uint32_t next;
    uint32_t end;
    uint32_t completed;
    uint32_t active_size;
    uint8_t active;
} erase_state_t;

static erase_state_t erase_state;

typedef struct {
    volatile uint32_t cr;
    volatile uint32_t torr;
    volatile uint32_t ccvr;
    volatile uint32_t crr;
    volatile uint32_t stat;
    volatile uint32_t eoi;
} wdt_registers_t;

static void reboot_soc(void)
{
    volatile wdt_registers_t *const wdt0 =
        (volatile wdt_registers_t *)WDT0_BASE_ADDR;

    /* The vendored minimal sysctl driver deliberately omits SOC and WDT reset
       cases.  Arm WDT0 directly, matching the Kendryte SDK reboot sequence. */
    sysctl->peri_reset.wdt0_reset = 1U;
    usleep(10U);
    sysctl->peri_reset.wdt0_reset = 0U;
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_WDT0, 0U);
    sysctl_clock_enable(SYSCTL_CLOCK_WDT0);
    sysctl->power_sel.power_mode_sel6 = 1U; /* WDT_RESET_ALL */
    wdt0->cr = 0U;
    wdt0->torr = 0U;
    wdt0->crr = 0x76U;
    wdt0->cr = 0x03U; /* reset mode + enable */
    for(;;)
        __asm__ volatile("nop");
}

uint32_t isp_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    while(length--)
    {
        crc ^= *bytes++;
        for(uint8_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static uint8_t valid_payload(const isp_request_t *request, size_t frame_length)
{
    size_t payload_length;
    if(frame_length < 8U)
        return 0U;
    payload_length = frame_length - 8U;
    return request->checksum == isp_crc32((const uint8_t *)request + 8U,
                                          payload_length);
}

static uint8_t erase_kick(void)
{
    uint32_t remaining;
    if(erase_state.next >= erase_state.end)
        return 0U;
    remaining = erase_state.end - erase_state.next;
    if((erase_state.next & (BLOCK_SIZE - 1U)) == 0U && remaining >= BLOCK_SIZE)
    {
        erase_state.active_size = BLOCK_SIZE;
        flash_64k_block_erase(erase_state.next);
    }
    else
    {
        erase_state.active_size = SECTOR_SIZE;
        flash_sector_erase(erase_state.next);
    }
    erase_state.next += erase_state.active_size;
    return 1U;
}

static uint8_t erase_status(void)
{
    if(!erase_state.active)
        return ISP_OK;
    if(flash_is_busy() == FLASH_BUSY)
        return ISP_BUSY;
    erase_state.completed += erase_state.active_size;
    ui_erasing(erase_state.completed, erase_state.end - erase_state.start);
    if(erase_kick())
        return ISP_BUSY;
    erase_state.active = 0U;
    ui_flash_ready();
    return ISP_OK;
}

static uint8_t handle_request(const isp_request_t *request, size_t frame_length,
                              uint8_t *reboot)
{
    uint32_t address;
    uint32_t length;
    *reboot = 0U;

    switch((uint8_t)request->operation)
    {
    case ISP_NOP:
        return ISP_OK;

    case ISP_FLASH_INIT:
        if(frame_length < 16U || !valid_payload(request, frame_length))
        {
            ui_error("CRC");
            return ISP_BAD_CHECKSUM;
        }
        ui_initializing();
        if(flash_init((uint8_t)request->address) != FLASH_OK)
        {
            ui_error("FLASH INIT");
            return ISP_BAD_INITIALIZATION;
        }
        flash_enable_quad_mode();
        flash_initialized = 1U;
        ui_flash_ready();
        return ISP_OK;

    case ISP_BAUDRATE:
        if(frame_length < 20U || request->data_length != 4U ||
           !valid_payload(request, frame_length))
        {
            ui_error("CRC");
            return ISP_BAD_CHECKSUM;
        }
        uart_configure(UART_PORT, *(const uint32_t *)request->data,
                       8U, UART_STOP_1, UART_PARITY_NONE);
        return ISP_OK;

    case ISP_FLASH_ERASE:
        if(!flash_initialized)
            return ISP_BAD_INITIALIZATION;
        if(frame_length < 16U || !valid_payload(request, frame_length))
        {
            ui_error("CRC");
            return ISP_BAD_CHECKSUM;
        }
        address = request->address;
        length = request->data_length;
        if(length == 0U || address >= FLASH_CAPACITY ||
           length > FLASH_CAPACITY - address ||
           (address & (SECTOR_SIZE - 1U)) != 0U)
        {
            ui_error("ERASE ADDR");
            return ISP_BAD_ADDRESS;
        }
        erase_state.start = address;
        erase_state.next = address;
        erase_state.end = (address + length + SECTOR_SIZE - 1U) & ~(SECTOR_SIZE - 1U);
        erase_state.completed = 0U;
        erase_state.active_size = 0U;
        erase_state.active = 1U;
        ui_erasing(0U, erase_state.end - erase_state.start);
        erase_kick();
        return ISP_BUSY;

    case ISP_FLASH_STATUS:
        return erase_status();

    case ISP_FLASH_WRITE:
        if(!flash_initialized)
            return ISP_BAD_INITIALIZATION;
        if(frame_length < 16U || !valid_payload(request, frame_length))
        {
            ui_error("CRC");
            return ISP_BAD_CHECKSUM;
        }
        if(erase_state.active && erase_status() == ISP_BUSY)
            return ISP_BUSY;
        address = request->address;
        length = request->data_length;
        if(length == 0U || length > ISP_MAX_BLOCK ||
           address > FLASH_CAPACITY - length ||
           (address & (SECTOR_SIZE - 1U)) != 0U ||
           frame_length != 16U + length)
        {
            ui_error("WRITE ADDR");
            return ISP_BAD_ADDRESS;
        }
        if(!ui_validate_write(address, request->data, length))
        {
            ui_error("BAD HEADER");
            return ISP_BAD_ADDRESS;
        }
        flash_sector_erase(address);
        while(flash_is_busy() == FLASH_BUSY) { }
        if(flash_write_data(address, (uint8_t *)request->data, length) != FLASH_OK)
        {
            ui_error("FLASH WRITE");
            return ISP_FLASH_ERROR;
        }
        ui_write_complete(address, request->data, length);
        return ISP_OK;

    case ISP_REBOOT:
        ui_done();
        *reboot = 1U;
        return ISP_OK;

    default:
        ui_error("BAD CMD");
        return ISP_INVALID_COMMAND;
    }
}

int main(void)
{
    extern uint32_t _bss;
    extern uint32_t _ebss;
    for (uint32_t *word = &_bss; word < &_ebss; ++word)
        *word = 0;

    uint8_t byte;
    fpioa_set_function(4, FUNC_UART3_RX);
    fpioa_set_function(5, FUNC_UART3_TX);
    uart_init(UART_PORT);
    uart_configure(UART_PORT, 115200U, 8U, UART_STOP_1, UART_PARITY_NONE);
    ui_init();

    for(;;)
    {
        if(uart_receive_data(UART_PORT, (char *)&byte, 1U) != 1U)
            continue;
        if(slip_decode_byte(&decoder, byte, rx_buffer, sizeof(rx_buffer)))
        {
            isp_response_t response;
            uint8_t reboot = 0U;
            response.operation = rx_buffer[0];
            if(decoder.length < 8U)
            {
                response.reason = ISP_BAD_LENGTH;
                ui_error("BAD LENGTH");
            }
            else
            {
                isp_request_t *request = (isp_request_t *)rx_buffer;
                response.reason = handle_request(request, decoder.length, &reboot);
            }
            if(reboot)
            {
                msleep(500U);
                reboot_soc();
            }
            slip_send(&response, sizeof(response));
        }
    }
}
