#!/usr/bin/env bash
# =============================================================================
# BOOTSEL (UF2) upload helper
# Compiles the firmware and copies firmware.uf2 to mounted BOOTSEL storage.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CMAKE_BUILD_DIR="$PROJECT_DIR/.build/cmake"
FIRMWARE_UF2="$PROJECT_DIR/.build/firmware.uf2"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
MONITOR="$SCRIPT_DIR/serial-persistent.py"
MONITOR_LOG="/tmp/rp2040-doom-persistent-monitor.log"
MONITOR_PATTERN="serial-persistent.py"
BOOTSEL_RETRIES="${DOOM_UPLOAD_BOOTSEL_RETRIES:-20}"
BOOTSEL_RESET="${DOOM_UPLOAD_BOOTSEL_RESET:-1}"
USER_NAME="${USER:-$(id -un)}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

MONITOR_WAS_RUNNING=0

pause_monitor() {
    if pgrep -f "$MONITOR_PATTERN" >/dev/null 2>&1; then
        MONITOR_WAS_RUNNING=1
        info "Pausing serial monitor for upload..."
        pkill -f "$MONITOR_PATTERN" 2>/dev/null || true
        for _ in $(seq 1 30); do
            pgrep -f "$MONITOR_PATTERN" >/dev/null 2>&1 || break
            sleep 0.1
        done
    fi
}

resume_monitor() {
    if [[ "$MONITOR_WAS_RUNNING" == "1" ]] && [[ -f "$MONITOR" ]]; then
        if ! pgrep -f "$MONITOR_PATTERN" >/dev/null 2>&1; then
            info "Resuming serial monitor..."
            nohup python3 "$MONITOR" -m pico >"$MONITOR_LOG" 2>&1 &
        fi
    fi
}
trap resume_monitor EXIT

find_bootsel_mount() {
    local base name mount

    for base in "/media/$USER_NAME" "/run/media/$USER_NAME"; do
        [[ -d "$base" ]] || continue
        for name in RPI-RP2 RP2350 RPI-RP2350; do
            mount=$(find "$base" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
            if [[ -n "$mount" ]]; then
                printf '%s\n' "$mount"
                return 0
            fi
        done
    done

    return 1
}

canonical_path() {
    local path="$1"
    if [[ -z "$path" ]]; then
        printf '\n'
        return 0
    fi
    readlink -f "$path" 2>/dev/null || printf '%s\n' "$path"
}

find_bootsel_block_device() {
    local name link

    for name in RPI-RP2 RP2350 RPI-RP2350; do
        link="/dev/disk/by-label/$name"
        if [[ -e "$link" || -L "$link" ]]; then
            canonical_path "$link"
            return 0
        fi
    done

    return 1
}

ensure_bootsel_mount() {
    local mount block_device

    mount="$(find_bootsel_mount || true)"
    if [[ -n "$mount" ]]; then
        printf '%s\n' "$mount"
        return 0
    fi

    block_device="$(find_bootsel_block_device || true)"
    if [[ -z "$block_device" ]]; then
        return 1
    fi

    if command -v udisksctl >/dev/null 2>&1; then
        udisksctl mount -b "$block_device" >/dev/null 2>&1 || true
    fi

    mount="$(find_bootsel_mount || true)"
    if [[ -n "$mount" ]]; then
        printf '%s\n' "$mount"
        return 0
    fi

    if command -v findmnt >/dev/null 2>&1; then
        mount="$(findmnt -nr -S "$block_device" -o TARGET 2>/dev/null | head -1)"
        if [[ -n "$mount" ]]; then
            printf '%s\n' "$mount"
            return 0
        fi
    fi

    return 1
}

wait_for_bootsel_mount() {
    local mount

    for ((i = 1; i <= BOOTSEL_RETRIES; i++)); do
        mount="$(ensure_bootsel_mount || true)"
        if [[ -n "$mount" ]]; then
            printf '%s\n' "$mount"
            return 0
        fi
        sleep 0.5
    done

    return 1
}

print_bootsel_help() {
    echo ""
    echo "  Instructions:"
    echo "  1. Unplug the board from USB"
    echo "  2. Hold the BOOTSEL button"
    echo "  3. Plug USB in while holding BOOTSEL"
    echo "  4. Release BOOTSEL - an RPI-RP2 drive should appear"
    echo "  5. Run this script again"
    echo ""
    echo "  Mounted drives in /media/$USER_NAME/:"
    ls "/media/$USER_NAME"/ 2>/dev/null || echo "    (none)"
    echo ""
    echo "  Mounted drives in /run/media/$USER_NAME/:"
    ls "/run/media/$USER_NAME"/ 2>/dev/null || echo "    (none)"
    echo ""
    echo "  BOOTSEL labels in /dev/disk/by-label/:"
    ls /dev/disk/by-label 2>/dev/null | grep -E '^(RPI-RP2|RP2350|RPI-RP2350)$' || echo "    (none)"
}

info "Configuring CMake..."
"$SCRIPT_DIR/configure-cmake.sh"

info "Compiling firmware..."
if ! cmake --build "$CMAKE_BUILD_DIR" --target firmware; then
    err "Compilation failed"
    exit 1
fi

echo ""
info "Searching for UF2 file..."
if [[ ! -f "$FIRMWARE_UF2" ]]; then
    err "No firmware.uf2 file found at $FIRMWARE_UF2"
    exit 1
fi
ok "Found: $FIRMWARE_UF2"

BOOT_PROBE_ONLY="OFF"
if [[ -f "$CMAKE_BUILD_DIR/CMakeCache.txt" ]]; then
    BOOT_PROBE_ONLY="$(sed -n 's/^DOOM_BOOT_PROBE_ONLY:BOOL=//p' "$CMAKE_BUILD_DIR/CMakeCache.txt" | tail -1)"
fi
case "$BOOT_PROBE_ONLY" in
    ON|TRUE|true|1) warn "DOOM_BOOT_PROBE_ONLY is enabled; uploading firmware only" ;;
    *)             warn "WHX payload is not uploaded by this task" ;;
esac

pause_monitor

MOUNT="$(ensure_bootsel_mount || true)"
if [[ -z "$MOUNT" && "$BOOTSEL_RESET" != "0" && "$BOOTSEL_RESET" != "false" ]]; then
    info "Requesting BOOTSEL via USB 1200 bps touch..."
    if ! python3 "$SCRIPT_DIR/enter-bootsel.py" --settings "$SETTINGS_FILE"; then
        warn "1200 bps reset did not confirm; waiting for an already-mounted BOOTSEL drive"
    fi
fi

info "Searching for BOOTSEL drive..."
MOUNT="$(wait_for_bootsel_mount || true)"
if [[ -z "$MOUNT" ]]; then
    err "BOOTSEL drive not found"
    print_bootsel_help
    exit 1
fi

info "Copying to $MOUNT..."
cp "$FIRMWARE_UF2" "$MOUNT/"
sync

echo ""
ok "UF2 upload finished"
ok "File: $(basename "$FIRMWARE_UF2") -> $MOUNT/"
# resume_monitor runs from the EXIT trap.
