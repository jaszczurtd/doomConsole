# Building and uploading DoomConsole

[Polski](BUILDING.pl.md) · [Project overview](../README.md)

Run the commands from the DoomConsole directory. Keep JaszczurHAL at
`../JaszczurHAL` and prepare its managed tools with `runmefirst.sh`
on Linux or `runmefirst.ps1` on Windows. This supplies the Pico SDK,
compilers, CMake, Ninja and picotool used by the project.

## Choose an assembly and build

[device_tree.json](../device_tree.json) defines `pico2w` (`rp2350-arm`) and
`rp2040-plus` (`rp2040`). The project manifest defaults to `pico2w`.
`Project: Select board` stores a local choice in the ignored
`.vscode/jaszczurhal.local.json`. An explicit `--assembly` selects the assembly
for that command, independently of the local choice.

```bash
../JaszczurHAL/vscode/entry/jh-vscode config-dump --project . --assembly pico2w
../JaszczurHAL/vscode/entry/jh-vscode build --project . --assembly pico2w
../JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project . --assembly pico2w
```

On Windows, use the native launcher:

```powershell
& ..\JaszczurHAL\vscode\entry\jh-vscode.cmd build --project . --assembly pico2w
```

Replace `pico2w` with `rp2040-plus` for that assembly. Firmware outputs are
`.build/<target>/<assembly>/firmware.elf`, `firmware.bin` and `firmware.uf2`.
The linker map remains in the CMake build tree; `config-dump` shows the
resolved build configuration.

## Display and build options

[hal_project_config.h](../hal_project_config.h) declares the variants:

| Variant | Assemblies | Result |
| --- | --- | --- |
| Default, no `--variant` | Both | ILI9341 game firmware |
| `ST7796S` | `pico2w` only | ST7796S game firmware |
| `BOOT_PROBE` | Both | Minimal application-entry smoke test |

```bash
../JaszczurHAL/vscode/entry/jh-vscode build --project . --assembly pico2w --variant ST7796S
```

Variant outputs add `variants/<id>/` below the assembly output directory.
`BOOT_PROBE` exercises startup and the status LED; it does not run the game.

`DOOM_HIGHRES_SCENE=1` renders 320×240 on ILI9341 or 480×320 on ST7796S;
`0` uses 320×200. `DOOM_DUAL_CORE_COLUMNS` and `DOOM_RENDER_ASYNC_PLANES`
default to `0`. `DOOM_VIDEO_SYNC_FLUSH=0` keeps asynchronous display transfer.
See [performance notes](../port_issues.md) before enabling renderer experiments.

Change pins and the requested SPI frequency in `device_tree.json`. The default
request is 50 MHz; `JH_ILI9341_SPI_DEFAULT_HZ` and `JH_ST77XX_SPI_DEFAULT_HZ`
carry it to the drivers. [doom_main_config.h](../doom_main_config.h) controls
rotation, color order, audio buffering, memory budgets and diagnostics. Its
default overclock requests are 300 MHz on RP2350 and 250 MHz on RP2040, at
1.30 V (`DOOM_SYS_OVERCLOCK`, `DOOM_SYS_CLOCK_KHZ`, `DOOM_SYS_VREG_VOLTAGE`).
Actual SPI frequency depends on the clock divisors. Verify stability on the
connected board and display when changing clocks.

## Upload firmware

Use `Project: Upload` for the selected assembly or
`Project: Upload (UF2 / BOOTSEL)` to copy its UF2 to the BOOTSEL drive.
The default RP upload strategy is UF2; inspect `config-dump` for local overrides.
The image comes from the assembly output directory, including the variant when
selected.

Game data requires the separate upload below. A 2 MiB Raspberry Pi Pico does
not fit this project's flash layout. A different board needs a matching
assembly and enough flash.

## Upload game data

The firmware reads compressed WHD/WHX data from flash; it cannot use a normal
WAD directly. The repository includes `doom1.whx`, and converter sources are in
`src/whd_gen/`. The current `WHD_SUPER_TINY=1` format uses the `IWHX` magic.

Put the 4 MiB target in BOOTSEL mode, then run:

```bash
python3 scripts/upload_whx.py
```

```powershell
py -3 scripts/upload_whx.py
```

The helper loads at `0x10200000`, verifies the bytes, and reboots only after
both operations succeed. It reserves the final 8192 bytes of flash for KV:
`[0x103fe000, 0x10400000)`. This is two separately erasable 4 KiB banks,
configured by `HAL_RP_FLASH_EEPROM_SIZE`. Payload data must end before them.

Use `--serial <flash-id>` when multiple RP boards are in BOOTSEL. The helper
passes the selection to load, verify and reboot. Its `--flash-size` check uses
the declared capacity; it does not detect the board's physical capacity.

```bash
python3 scripts/upload_whx.py --dry-run
python3 scripts/upload_whx.py --no-reboot
```

`--dry-run` checks the file, tool path and flash bounds without accessing USB.
`--no-reboot` leaves the board in BOOTSEL. A different image can be passed as
the positional argument. If you change the firmware's `DOOM_WHD_FLASH_ADDR`,
also pass the same `--address` or environment variable `DOOM_WHD_FLASH_ADDR`
to the uploader. It does not evaluate `doom_main_config.h`.

The uploader reads the numeric reservation from `hal_project_config.h`.
A larger compiler override must also be passed as `--reserved-tail-size`;
values smaller than the header reservation are rejected. Use `--picotool`
or `--jaszczurhal-root` if managed-tool discovery needs an explicit path.

## Editor and host checks

Use `Project: Build`, `Project: Build (Debug)`, `Project: Serial Monitor`,
`Project: Debug Probe Monitor` and `Project: Refresh IntelliSense` for the
selected assembly. `Project: Upload WHX Payload` runs the uploader above.
Run and Debug contains launch profiles for each supported assembly/variant
using a CMSIS-DAP probe. Windows HAL setup configures OpenOCD and GNU Arm tools.

The project manifest, tasks, settings and launch profiles are versioned.
Keep the serial port and local assembly choice in `.vscode/jaszczurhal.local.json`.
IntelliSense uses the compile database produced by `jh-vscode`. After shared
task-registry changes, regenerate and check the editor files:

```bash
python3 scripts/sync_vscode_project.py
python3 scripts/sync_vscode_project.py --check
python3 -m unittest discover -s tests -p 'test_*.py'
```

On Windows use `py -3` in place of `python3`. Host tests cover configuration,
cache behavior, input/menu logic, upload commands and editor files. They require
a host C compiler and CMake as well as the neighboring HAL checkout.
The [Windows workflow](../.github/workflows/windows-firmware.yml) also builds
Pico 2 W firmware and refreshes IntelliSense.

Physical input, latency, reconnect and persistent storage require separate
[C8](../tests/C8_HARDWARE_GATE.md) and [C9](../tests/C9_HARDWARE_GATE.md) runs.
Host checks do not establish those results.
