# HuskyLens ISP Stub

[![CI](https://github.com/aztechell/huskylens-isp-stub/actions/workflows/ci.yml/badge.svg)](https://github.com/aztechell/huskylens-isp-stub/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/aztechell/huskylens-isp-stub)](https://github.com/aztechell/huskylens-isp-stub/releases/latest)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Open-source second-stage K210 ISP with separate HuskyLens and Sipeed Maix Cube
builds. Both run from SRAM at `0x80000000`, remain compatible with the kflash
protocol, write onboard SPI flash, and show update progress on the ST7789
screen: 320x240 on HuskyLens, 240x240 on Cube.

The UI shows `WAITING FOR HOST`, flash initialization, erase activity,
flashing percentage and progress bar, finalization, errors, and `DONE` before
the device reboots.

## Download

Use the reproducible checked-in binary for the selected board:

| Board | Binary |
| --- | --- |
| HuskyLens SEN0305 | `isp_prog_huskylens.bin` |
| Sipeed Maix Cube 2757 | `isp_prog_cube.bin` |

`artifacts.json` records the sizes and SHA-256 checksums. The uploader selects
the binary from its board profile; the stub does not auto-detect hardware.
The latest tagged release may predate Cube support; use the checked-in pair
until a release containing both artifacts is published.

## Build

The minimal Apache-2.0 K210 BSP needed by the stub is vendored under
`third_party/minsdk`. Bootstrap downloads Kendryte SDK commit
`02576ba67e8797444f3ee3f34c625b5ed048e707` and toolchain
`8.2.0-20190409` for Windows or Linux:

```powershell
py tools\bootstrap_deps.py
py tools\build.py --board all
py -m unittest discover -s tests -v
```

On Linux:

```bash
python3 tools/bootstrap_deps.py
python3 tools/build.py --board all
python3 -m unittest discover -s tests -v
```

`--board huskylens` (the default) and `--board cube` build one target;
`--board all` builds both in isolated directories. Outputs:

| Board | BIN | ELF / map |
| --- | --- | --- |
| HuskyLens | `build/isp_prog_huskylens.bin` | `build/isp_stub.elf`, `build/isp_stub.map` |
| Cube | `build/isp_prog_cube.bin` | `build/isp_stub_cube.elf`, `build/isp_stub_cube.map` |

Production builds refresh the matching root binary and its manifest entry.
Each target must fit 64 KiB, start/load at `0x80000000`, and fit in SRAM with
its BSS and stacks. `--headless` and `--lcd-init-only` produce separate
diagnostic artifacts and never overwrite production binaries or the manifest.

Native host tests run the actual C UI/font, Cube LCD transport and DMA register
setup with fake hardware. Set `ISP_PREVIEW_DIR` to a directory when running
the tests to save PPM previews for both screen sizes.

Pushing a `v*` tag rebuilds and tests both targets, then publishes both binaries,
checksums, ELFs, maps and the manifest as GitHub release assets.

## Protocol

Frames use the kflash SLIP envelope and CRC32 request layout. Supported
commands are `D2` NOP, `D4` flash write, `D5` reboot, `D6` baud switch,
`D7` flash initialization, `D8` range erase, and `D9` erase status. Responses
include `E0` OK, `E2` bad CRC, and `E7` busy. Existing uploaders require no new
commands.

The first address-zero block contains the K210 image header. Its little-endian
raw length at bytes 1-4 gives `wire_size = raw_size + 37`; committed contiguous
blocks drive the displayed percentage. Retries do not count twice. Later
KFPKG segments display `FINALIZING` until reboot.

## HuskyLens wiring

| Function | IO / peripheral |
|---|---|
| ISP UART RX/TX | IO4 / IO5, UART3 |
| LCD DC | IO18 / GPIOHS15 |
| LCD CS | IO19 / SPI0 SS3 |
| LCD SCLK | IO20 / SPI0 SCLK |
| LCD MOSI | IO21 / SPI0 D0 |
| LCD reset | IO22 / GPIOHS14 |
| LCD backlight | IO24 / GPIOHS13 |
| Boot flash | internal SPI3 |

See [NOTICE.md](NOTICE.md) for upstream provenance. Source is Apache-2.0.

## Maix Cube wiring

| Function | IO / peripheral |
| --- | --- |
| ISP UART RX/TX | IO4 / IO5, UART3 |
| LCD DC | IO38 / GPIOHS15 |
| LCD CS | IO36 / SPI0 SS3 |
| LCD WR | IO39 / SPI0 SCLK, 15 MHz |
| LCD reset | IO37 / GPIOHS14 |
| LCD backlight | IO17 / GPIOHS13, active-low |
| LCD D0-D7 | fixed SPI0/DVP octal bus, banks 6/7 at 1.8 V |
| Boot flash | internal SPI3 |

Cube uses complete DMA0 transactions from an uncached staging buffer to avoid
gaps on the parallel bus. The panel uses RGB565, MADCTL `0x60` and no RAM
offset. LCD/DMA waits are bounded; a display failure disables rendering while
the ISP protocol remains available. Camera and PMIC setup are not required.

On 2026-10-30, Cube SRAM D2/E0 startup and firmware programming were checked on
COM15. The user confirmed `MAIX CUBE UPDATE` / `WAITING FOR HOST`, followed by
increasing percentages and a filling progress bar during programming at
460800 baud. HuskyLens physical regression remains a separate acceptance step.

The minimal BSP enables the DMA clock gate; a native regression test runs the
actual BSP clock dispatcher instead of mocking it.

DMA completion accepts the normal source/destination transaction flags
(bits 3/4, including observed status `0x12`). Only the source/destination/LLI
error flags in bits 5-12 disable rendering; completion alone is not an error.
