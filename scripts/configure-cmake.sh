#!/usr/bin/env bash
# =============================================================================
# Configure the CMake-based firmware build from VS Code Arduino settings.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
CMAKE_BUILD_DIR="$PROJECT_DIR/.build/cmake"
ENSURE_CORE_SCRIPT="$SCRIPT_DIR/ensure-core-version.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

read_setting() {
    local key="$1"
    local default="${2:-}"
    python3 - "$SETTINGS_FILE" "$key" "$default" <<'PYEOF'
import json
import os
import sys

settings_file, key, default = sys.argv[1], sys.argv[2], sys.argv[3]
if not os.path.isfile(settings_file):
    print(default)
    raise SystemExit(0)

with open(settings_file) as f:
    settings = json.load(f)

value = settings.get(key, default)
if isinstance(value, bool):
    value = "true" if value else "false"
print(value)
PYEOF
}

# Read a settings.json key and normalise it to the 0/1 that CMakeLists expects.
read_bool01() {
    local value
    value="$(read_setting "$1" "$2")"
    if [[ "$value" == "true" || "$value" == "1" ]]; then
        echo 1
    else
        echo 0
    fi
}

main() {
    local cli fqbn sketchbook upload_port verbose jh_root boot_probe_only

    cli="$(read_setting "arduino.cliPath" "arduino-cli")"
    [[ -n "$cli" ]] || cli="arduino-cli"

    fqbn="$(read_setting "arduino.fqbn" "")"
    if [[ -z "$fqbn" ]]; then
        err "Missing arduino.fqbn in .vscode/settings.json"
        exit 1
    fi

    sketchbook="$(read_setting "arduino.sketchbookPath" "")"
    upload_port="$(read_setting "arduino.uploadPort" "")"
    verbose="$(read_setting "arduino.verbose" "false")"
    boot_probe_only="$(read_setting "doom.bootProbeOnly" "false")"
    jh_root="${JH_ROOT:-$(read_setting "jaszczurhal.root" "$PROJECT_DIR/../libraries/JaszczurHAL")}"

    # Render feature toggles are sticky CMake cache variables; unless we pass
    # them explicitly on EVERY configure, a stale per-machine cache value wins and
    # diverges from the committed source.  settings.json is the single source of
    # truth; normalise each bool to the 0/1 that CMakeLists validates.
    local dual_core async_planes video_sync_flush
    dual_core="$(read_bool01 "doom.dualCoreColumns" "false")"
    async_planes="$(read_bool01 "doom.asyncPlanes" "false")"
    video_sync_flush="$(read_bool01 "doom.videoSyncFlush" "false")"

    # Detect the target chip from the FQBN.  Earle's core keeps the "rp2040"
    # platform id even for RP2350 boards, so match the board name: rpipico2 /
    # rpipico2w / waveshare_rp2350_* carry "pico2" or "rp2350".
    local target sys_clock_default spi_hz_default
    if [[ "$fqbn" == *pico2* || "$fqbn" == *rp2350* ]]; then
        target="RP2350"
        sys_clock_default="300000"    # M33 overclock
        spi_hz_default="50000000"     # 300 MHz clk_peri / 6 = 50 MHz (clean)
    else
        target="RP2040"
        sys_clock_default="250000"
        spi_hz_default="41666666"     # 250 MHz clk_peri / 6 = 41.67 MHz (clean, no flicker)
    fi
    # settings.json may override the target defaults for fine-tuning.
    local sys_clock_khz tft_spi_hz
    sys_clock_khz="$(read_setting "doom.sysClockKhz" "$sys_clock_default")"
    tft_spi_hz="$(read_setting "doom.tftSpiHz" "$spi_hz_default")"

    info "Configuring CMake firmware build"
    info "  CLI:         $cli"
    info "  FQBN:        $fqbn"
    info "  Target:      $target"
    info "  JaszczurHAL: $jh_root"
    info "  Boot probe:  $boot_probe_only"
    info "  Dual-core columns: $dual_core"
    info "  Async planes (core1): $async_planes"
    info "  Video sync flush: $video_sync_flush"
    info "  System clock: ${sys_clock_khz} kHz"
    info "  TFT SPI request: ${tft_spi_hz} Hz"

    if [[ -f "$ENSURE_CORE_SCRIPT" ]]; then
        info "Ensuring required Arduino core version..."
        bash "$ENSURE_CORE_SCRIPT" --cli "$cli" --fqbn "$fqbn"
    fi

    cmake -S "$PROJECT_DIR" -B "$CMAKE_BUILD_DIR" \
        -DARDUINO_CLI="$cli" \
        -DARDUINO_FQBN="$fqbn" \
        -DARDUINO_SKETCHBOOK="$sketchbook" \
        -DARDUINO_UPLOAD_PORT="$upload_port" \
        -DARDUINO_VERBOSE="$verbose" \
        -DDOOM_BOOT_PROBE_ONLY="$boot_probe_only" \
        -DDOOM_DUAL_CORE_COLUMNS="$dual_core" \
        -DDOOM_RENDER_ASYNC_PLANES="$async_planes" \
        -DDOOM_VIDEO_SYNC_FLUSH="$video_sync_flush" \
        -DDOOM_SYS_CLOCK_KHZ="$sys_clock_khz" \
        -DJH_ILI9341_SPI_DEFAULT_HZ="$tft_spi_hz" \
        -DJH_ROOT="$jh_root"

    ok "CMake configured: $CMAKE_BUILD_DIR"
}

main "$@"
