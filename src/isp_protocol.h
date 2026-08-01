#ifndef HUSKY_ISP_PROTOCOL_H
#define HUSKY_ISP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define ISP_MAX_BLOCK 4096U
#define ISP_RX_CAPACITY (ISP_MAX_BLOCK + 32U)

enum isp_operation {
    ISP_DEBUG = 0xD1,
    ISP_NOP = 0xD2,
    ISP_FLASH_WRITE = 0xD4,
    ISP_REBOOT = 0xD5,
    ISP_BAUDRATE = 0xD6,
    ISP_FLASH_INIT = 0xD7,
    ISP_FLASH_ERASE = 0xD8,
    ISP_FLASH_STATUS = 0xD9,
};

enum isp_reason {
    ISP_OK = 0xE0,
    ISP_BAD_LENGTH = 0xE1,
    ISP_BAD_CHECKSUM = 0xE2,
    ISP_INVALID_COMMAND = 0xE3,
    ISP_BAD_INITIALIZATION = 0xE4,
    ISP_BAD_ADDRESS = 0xE5,
    ISP_FLASH_ERROR = 0xE6,
    ISP_BUSY = 0xE7,
};

typedef struct __attribute__((packed)) {
    uint16_t operation;
    uint16_t reserved;
    uint32_t checksum;
    uint32_t address;
    uint32_t data_length;
    uint8_t data[];
} isp_request_t;

typedef struct __attribute__((packed)) {
    uint8_t operation;
    uint8_t reason;
} isp_response_t;

uint32_t isp_crc32(const void *data, size_t length);

#endif
