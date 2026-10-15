# DoomConsole

[Polski](README.pl.md)

DoomConsole runs Doom on RP2040 and RP2350 boards with a TFT display, PWM audio
and GPIO controls. The Pico 2 W build also supports a Bluetooth Classic HID
gamepad with saved pairing. Firmware uses [JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)
and the official Pico SDK.

| Assembly | Board | Display |
| --- | --- | --- |
| `pico2w` (default) | Raspberry Pi Pico 2 W | ILI9341, 320×240; ST7796S variant, 480×320 |
| `rp2040-plus` | Waveshare RP2040-Plus, 4 MiB flash | ILI9341, 320×240 |

The classic 320×200 scene is available with `DOOM_HIGHRES_SCENE=0`.
Game data is a separate WHX flash payload; uploading firmware alone does not
install it. The configured layout requires 4 MiB flash. The renderer remains
under development, and the PCB is not ready.

## Getting started

Follow [building and uploading](docs/BUILDING.md), then connect the hardware
using the [wiring and controls guide](hardware_layout.md).

- [Performance, diagnostics and remaining work](port_issues.md)
- [C8: gamepad hardware checks](tests/C8_HARDWARE_GATE.md)
- [C9: persistent pairing hardware checks](tests/C9_HARDWARE_GATE.md)

The build recipe is [CMakeLists.txt](CMakeLists.txt). Wiring comes from
[device_tree.json](device_tree.json); build options come from
[hal_project_config.h](hal_project_config.h), with port settings in
[doom_main_config.h](doom_main_config.h). Active platform code is in
`src/jaszczurhal/`, game code in `src/doom/`, and the data converter in
`src/whd_gen/`. `_unused/` holds code and documents outside the active port.

## Demos

These recordings show earlier builds; their resolution may differ from the default.

- [RP2040, ILI9341, 320×200](https://www.youtube.com/watch?v=PHE-ADc1qWk)
- [RP2350, ILI9341, 320×200](https://www.youtube.com/watch?v=I1niNYLehJY)
- [RP2350, ST7796S, 480×320](https://www.youtube.com/watch?v=YNoUZEpIq5A)

## Origin and licensing

This port is based on Graham Sanderson's
[kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom), derived from
[Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom). It retains
the compressed game-data format and renderer work, with platform services
provided through JaszczurHAL.

See [COPYING.md](COPYING.md) and the notices in individual source files,
including [the ADPCM license](src/adpcm-xq/license.txt). Preserve the upstream
attribution when redistributing modified code. Game-data distribution has its
own licensing requirements.
