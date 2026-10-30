# Notices

The ISP protocol, SLIP implementation, and SPI-flash driver are derived from
[loboris/ktool](https://github.com/loboris/ktool), Copyright 2020 LoBo,
licensed under Apache License 2.0.

The build uses the Kendryte Standalone SDK, Copyright 2018 Canaan Inc.,
licensed under Apache License 2.0. The pinned SDK commit is
`02576ba67e8797444f3ee3f34c625b5ed048e707`.

The vendored minimal BSP/build subset follows `loboris/ktool` commit
`0345aa90d9b3830641373fb4e3ce4edf45d0a46f`; only the K210 startup, clock,
FPIOA, UART, and timing support needed by this SRAM program is included.

The Cube LCD DMA0 implementation in `dmac_isp.c` is a polling-only subset
derived from `lib/drivers/dmac.c` in the same pinned Kendryte SDK. Its reset
wait is bounded and it uses uncached memory without allocation or interrupts.
The minimal BSP's clock dispatcher also enables/disables the DMA clock gate;
the original ktool subset had omitted that case.
The SDK's `dmac.h` and `plic.h` declarations retain their original licenses.
Cube LCD routing, octal transfer phases and panel initialization follow the
existing HackyLens Cube port and Sipeed MaixPy-v1 board/driver definitions at
`d8901fd2272f000226f1c1037c1eb7c412b88e66`.
