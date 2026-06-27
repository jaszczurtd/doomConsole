# Hardware Layout

## Doom GPIO Input

The current JaszczurHAL input backend uses simple GPIO buttons connected
between the signal pin and GND.

Default electrical layout:

- Input mode: `HAL_GPIO_INPUT_PULLUP`
- Active state: low
- Override polarity with `DOOM_INPUT_ACTIVE_LOW`

Default button map:

| Function | GPIO pin | Doom key | Override macro |
| --- | ---: | --- | --- |
| Up | 2 | `KEY_UPARROW` | `DOOM_INPUT_PIN_UP` |
| Down | 3 | `KEY_DOWNARROW` | `DOOM_INPUT_PIN_DOWN` |
| Left | 4 | `KEY_LEFTARROW` | `DOOM_INPUT_PIN_LEFT` |
| Right | 5 | `KEY_RIGHTARROW` | `DOOM_INPUT_PIN_RIGHT` |
| Fire | 7 | `KEY_RCTRL` | `DOOM_INPUT_PIN_FIRE` |
| Use | 8 | Space | `DOOM_INPUT_PIN_USE` |
| Menu | 9 | `KEY_ESCAPE` | `DOOM_INPUT_PIN_MENU` |
| Accept | 10 | `KEY_ENTER` | `DOOM_INPUT_PIN_ACCEPT` |
| Back | 11 | `KEY_BACKSPACE` | `DOOM_INPUT_PIN_BACK` |

Implementation: `src/jaszczurhal/doom_input_hal.c`.

## TFT Display

Test platform: Waveshare RP2040-Plus 4 MB with a 2.8" SPI TFT using the
ILI9341 controller.

Default display wiring:

| Display signal | RP2040 GPIO | Notes |
| --- | ---: | --- |
| `SCK` / `CLK` | 18 | SPI0 clock, JaszczurHAL default bus 0 |
| `MOSI` / `SDA` / `DIN` | 19 | SPI0 TX, data from RP2040 to display |
| `MISO` / `SDO` | 16 | Optional for display-only ILI9341 use |
| `CS` | 17 | `DOOM_HAL_TFT_CS_PIN` |
| `DC` / `D/C` / `RS` | 20 | `DOOM_HAL_TFT_DC_PIN` |
| `RST` / `RESET` | 21 | `DOOM_HAL_TFT_RST_PIN` |
| `VCC` | 3V3 | Use 3.3 V logic |
| `GND` | GND | Common ground |
| `LED` / `BL` | 3V3 | Backlight always on for the current firmware |

The display SD-card and touch-controller pins are not used by the current
firmware.

## Doom WHD/WHDX Flash Payload

The current JaszczurHAL port keeps the game data as a raw XIP-readable flash
payload programmed separately from the firmware image.

Default layout:

| Region | Address | Notes |
| --- | ---: | --- |
| Firmware XIP base | `0x10000000` | `DOOM_FLASH_XIP_BASE` |
| WHD/WHDX payload | `0x10200000` | `DOOM_WHD_FLASH_ADDR` / `TINY_WAD_ADDR` |
| Assumed flash end | `0x10400000` | `DOOM_FLASH_SIZE_BYTES=0x400000` |

This layout requires at least 4 MB of physical flash. The included
`doom1.whx` is about 1.8 MB, so it cannot share the original 2 MB Pico flash
with the current firmware.

For the RP2040-Plus 4 MB verification build, the Arduino-Pico board profile is
`rp2040:rp2040:generic:flash=4194304_2097152,usbstack=tinyusb`. Arduino-Pico
starts the nominal 2 MB filesystem partition at `0x101ff000`; this port leaves
the first 4 KB unused and stores the raw WHD/WHDX payload from `0x10200000`.
Do not format or mount that partition during hardware verification.

Hardware bring-up sequence:

1. Put the board in BOOTSEL mode.
2. Start the persistent serial monitor (`Ctrl+Shift+3`) if boot diagnostics are
   needed. The firmware opens USB CDC at `115200` and holds for 8 seconds before
   entering Doom, printing `[boot]` and `[video]` diagnostics.
3. Run VS Code task `Build: Upload` (`Ctrl+Shift+2`). It builds and copies
   `.build/firmware-with-whx.uf2`, a combined UF2 containing both
   `.build/firmware.uf2` and `.build/doom1.whx.uf2`.
   The upload script first looks for a mounted `RPI-RP2` drive, then tries to
   mount it through `udisksctl`, and finally falls back to direct
   `picotool load` if the USB BOOTSEL device is visible but no drive is
   mounted.

The active RP2040-Plus verification build defines `WHD_SUPER_TINY=1`, so the
payload magic expected at `0x10200000` is `IWHX`.

Manual fallback:

1. Flash firmware UF2 from `.build/firmware.uf2`.
2. Flash the WHX payload UF2 from `.build/doom1.whx.uf2`, or load the raw
   payload with:

   ```sh
   picotool load --ignore-partitions -v -t bin doom1.whx -o 0x10200000
   ```

If the payload is missing or has the wrong magic, the firmware boot screen
shows `Missing WHX` instead of entering the silent Doom breakpoint path.
