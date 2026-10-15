# Wiring and controls

[Polski](hardware_layout.pl.md) · [Project overview](README.md)

## Doom GPIO Input

The current JaszczurHAL input backend uses simple GPIO buttons connected
between the signal pin and GND.

Pins and polarity come from the button nodes of `device_tree.json`, the same
in both assemblies:

- Input mode: `HAL_GPIO_INPUT_PULLUP` (`pull: up`)
- Active state: low (`activeLow`)

Button map:

| Function | GPIO pin | Doom key | Node |
| --- | ---: | --- | --- |
| Up | 2 | `KEY_UPARROW` | `buttonUp` |
| Down | 3 | `KEY_DOWNARROW` | `buttonDown` |
| Left | 4 | `KEY_LEFTARROW` | `buttonLeft` |
| Right | 5 | `KEY_RIGHTARROW` | `buttonRight` |
| Fire | 7 | `KEY_RCTRL` | `buttonFire` |
| Use | 8 | Space | `buttonUse` |
| Menu | 9 | `KEY_ESCAPE` | `buttonMenu` |
| Accept | 10 | `KEY_ENTER` | `buttonAccept` |
| Back | 11 | `KEY_BACKSPACE` | `buttonBack` |

Implementation: `src/jaszczurhal/doom_input_hal.c`.

## Bluetooth Gamepad

Builds of the `pico2w` assembly (RP2350 ARM) enable the JaszczurHAL Bluetooth
Classic gamepad feature. The first supported controller is an 8BitDo Zero 2 in Android
D-input mode. GPIO and Bluetooth actions are combined, so the physical buttons
remain usable while a controller is connected.

The current buttonless build sets `BT_AUTOMATIC_PAIRING=1`. At startup it opens
one bounded 120-second pairing window only when application KV has no accepted
peer. Start the Zero 2 in Android D-input mode with `B+Start`, then press
`Select` to pair. A stored peer takes the reconnect path without reopening the
window. Builds with physical buttons may set the flag to `0` and hold GPIO
`Menu+Back` for three seconds to open the same window.

Once connected, hold `L+R`, add `Select`, and keep all three pressed for five
seconds to erase the accepted identity and link key. Pairing remains closed for the rest
of that runtime; the buttonless build opens a fresh empty-bond window after a
reboot. The nine GPIO buttons have no equivalent reset chord; `Menu+Accept+Back`
is not a reset operation in the current input implementation.
If persistent erase fails, automatic reconnect remains blocked until the
operator retries factory reset or explicitly starts a new pairing operation.

Zero 2 mapping:

| Zero 2 control | Doom action | Doom key |
| --- | --- | --- |
| D-pad up/down | Move forward/backward | Arrow up/down |
| D-pad left/right | Turn left/right | Arrow left/right |
| A | Fire | `KEY_RCTRL` |
| B | Use/open | Space |
| X | Accept/start game | `KEY_ENTER` |
| Start | Menu/pause through the menu | `KEY_ESCAPE` |
| Select | Back | `KEY_BACKSPACE` |
| Y + D-pad left/right | Strafe left/right | `KEY_RALT` + arrow left/right |
| L | Strafe left | `,` (`key_strafeleft`) |
| R | Strafe right | `.` (`key_straferight`) |

The adapter accepts input only after receiving a neutral snapshot for each new
connection generation. Disconnect, queue overflow, and polling errors release
all gamepad-owned actions. A valid persisted bond enables reconnect after a
watchdog reset, cold boot, or power loss without reopening pairing.

Implementation: `src/jaszczurhal/doom_gamepad_input.c`.

## TFT Display

Both assemblies, Raspberry Pi Pico 2 W and Waveshare RP2040-Plus 4 MB, wire a
2.8" SPI TFT with the ILI9341 controller the same way; the ST7796S variant uses
the same lines on the Pico 2 W. `device_tree.json` holds the wiring.

| Display signal | GPIO | Notes |
| --- | ---: | --- |
| `SCK` / `CLK` | 18 | SPI0 clock, JaszczurHAL default bus 0 |
| `MOSI` / `SDA` / `DIN` | 19 | SPI0 TX, data from the Pico to display |
| `MISO` / `SDO` | 16 | Optional for display-only ILI9341 use |
| `CS` | 17 | Node `tft`, pin `cs` |
| `DC` / `D/C` / `RS` | 20 | Node `tftDc` |
| `RST` / `RESET` | 21 | Node `tftReset` |
| `VCC` | 3V3 | Use 3.3 V logic |
| `GND` | GND | Common ground |
| `LED` / `BL` | 3V3 | Backlight always on for the current firmware |

The display SD-card and touch-controller pins are not used by the current
firmware.

## Audio and status LED

PWM audio comes from GP6 (`audio` in `device_tree.json`) on both assemblies.
Feed it to the appropriate audio output circuit; it is a logic-level PWM
signal. Buffer size, PWM resolution and mixer channels are configured in
`doom_main_config.h`.

The game uses the board LED for boot feedback and damage flashes. The
`BOOT_PROBE` variant blinks it once per second and draws a TFT heartbeat.

## Doom WHD/WHX Flash Payload

The current JaszczurHAL port keeps the game data as a raw XIP-readable flash
payload programmed separately from the firmware image.

Default layout:

| Region | Address | Notes |
| --- | ---: | --- |
| Firmware XIP base | `0x10000000` | `DOOM_FLASH_XIP_BASE` |
| WHD/WHX payload | `0x10200000` | `DOOM_WHD_FLASH_ADDR` / `TINY_WAD_ADDR` |
| Application data end | `0x103fe000` | First byte reserved for KV |
| Persistent KV | `0x103fe000`-`0x10400000` | Two 4 KiB banks |
| Physical flash end | `0x10400000` | `DOOM_FLASH_SIZE_BYTES=0x400000` |

This layout requires at least 4 MB of physical flash. The included
`doom1.whx` is about 1.72 MiB, so it cannot share the original 2 MB Pico flash
with the current firmware.

The `rp2040-plus` assembly uses the `rp2040-plus-4mb` board profile. The
profile selects the official Pico SDK board definition and validates that the
build declares a 4 MiB flash configuration. The raw WHD/WHX payload may occupy the
region from `0x10200000` up to, but not including, the application storage
reservation. The uploader enforces this boundary before accessing the board.

Hardware bring-up sequence:

1. Put the board in BOOTSEL mode.
2. Start the persistent serial monitor (`Ctrl+Shift+3`) if boot diagnostics are
   needed. The firmware opens USB CDC at `115200` and holds for 4 seconds (`DOOM_BOOT_DIAG_HOLD_MS`) before
   entering Doom, printing `[boot]` and `[video]` diagnostics.
3. Run `Project: Upload (UF2 / BOOTSEL)` to build and copy the UF2 of the
   selected assembly, e.g. `.build/rp2350-arm/pico2w/firmware.uf2`, to the
   BOOTSEL drive.
4. Put the board in BOOTSEL mode again and run `Project: Upload WHX Payload`.
   The task writes `doom1.whx` at `0x10200000` with picotool, verifies the
   payload, and reboots the board.

The build defines `WHD_SUPER_TINY=1`, so the payload magic expected at
`0x10200000` is `IWHX`.

For command-line upload and flash-bound checks, follow
[building and uploading](docs/BUILDING.md).

If the payload is missing or has the wrong magic, the firmware boot screen
shows `Missing WHX` instead of entering the silent Doom breakpoint path.
