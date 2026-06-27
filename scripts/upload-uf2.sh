#!/usr/bin/env bash
# =============================================================================
# BOOTSEL firmware UF2 upload helper
# Compiles the project and copies only firmware.uf2 to BOOTSEL storage.
# WHX payload upload is intentionally manual:
#   sudo scripts/upload-whx-picotool.sh
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CMAKE_BUILD_DIR="$PROJECT_DIR/.build/cmake"
FIRMWARE_UF2="$PROJECT_DIR/.build/firmware.uf2"
AUTO_BOOTSEL="${DOOM_UPLOAD_AUTO_BOOTSEL:-1}"
BOOTSEL_WAIT_SECONDS="${DOOM_UPLOAD_BOOTSEL_WAIT_SECONDS:-12}"
BOOTSEL_NAMES=(RPI-RP2 RP2350 RPI-RP2350)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

find_bootsel_mount() {
    local mount_root name found

    for mount_root in /media/"$USER" /run/media/"$USER" /mnt /Volumes; do
        [[ -d "$mount_root" ]] || continue

        for name in "${BOOTSEL_NAMES[@]}"; do
            found=$(find "$mount_root" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
            if [[ -n "$found" ]]; then
                echo "$found"
                return 0
            fi
        done
    done

    return 1
}

try_mount_bootsel_drive() {
    local dev label output

    command -v lsblk >/dev/null 2>&1 || return 1
    command -v udisksctl >/dev/null 2>&1 || return 1

    while read -r dev label; do
        case "$label" in
            RPI-RP2|RP2350|RPI-RP2350)
                info "Found BOOTSEL block device $dev; trying udisksctl mount..."
                if output=$(udisksctl mount -b "$dev" 2>&1); then
                    echo "$output"
                    return 0
                fi
                warn "udisksctl mount failed: $output"
                ;;
        esac
    done < <(lsblk -rpno NAME,LABEL 2>/dev/null)

    return 1
}

try_enter_bootsel_serial() {
    if [[ "$AUTO_BOOTSEL" == "0" || "$AUTO_BOOTSEL" == "false" ]]; then
        return 1
    fi

    command -v python3 >/dev/null 2>&1 || return 1

    info "BOOTSEL drive not found; trying USB CDC 1200 bps reset..."
    if python3 "$SCRIPT_DIR/enter-bootsel.py" \
        --settings "$PROJECT_DIR/.vscode/settings.json"; then
        return 0
    fi

    warn "Automatic BOOTSEL reset failed"
    return 1
}

wait_for_bootsel_mount() {
    local deadline

    deadline=$((SECONDS + BOOTSEL_WAIT_SECONDS))
    info "Waiting up to ${BOOTSEL_WAIT_SECONDS}s for BOOTSEL drive..."

    while (( SECONDS <= deadline )); do
        try_mount_bootsel_drive || true
        MOUNT="$(find_bootsel_mount || true)"
        if [[ -n "$MOUNT" ]]; then
            return 0
        fi
        sleep 0.5
    done

    return 1
}

# Compile through the CMake buildsystem.
info "Configuring CMake..."
"$SCRIPT_DIR/configure-cmake.sh"

info "Compiling firmware..."
if ! cmake --build "$CMAKE_BUILD_DIR" --target firmware; then
    err "Compilation failed"
    exit 1
fi

# Find firmware UF2 artifact.
echo ""
info "Searching for firmware UF2 file..."
UF2="$FIRMWARE_UF2"

if [[ ! -f "$UF2" ]]; then
    err "No firmware.uf2 file found at $UF2"
    exit 1
fi

ok "Found: $UF2"

BOOT_PROBE_ONLY="OFF"
if [[ -f "$CMAKE_BUILD_DIR/CMakeCache.txt" ]]; then
    BOOT_PROBE_ONLY="$(sed -n 's/^DOOM_BOOT_PROBE_ONLY:BOOL=//p' "$CMAKE_BUILD_DIR/CMakeCache.txt" | tail -1)"
fi

case "$BOOT_PROBE_ONLY" in
    ON|TRUE|true|1)
        warn "DOOM_BOOT_PROBE_ONLY is enabled; uploading firmware UF2 only"
        ;;
    *)
        warn "WHX payload is not uploaded by this VS Code task"
        warn "Use manually when needed: sudo $SCRIPT_DIR/upload-whx-picotool.sh"
        ;;
esac

# Find BOOTSEL drive
info "Searching for BOOTSEL drive..."
MOUNT="$(find_bootsel_mount || true)"

if [[ -z "$MOUNT" ]]; then
    warn "BOOTSEL USB device is not mounted as a drive"
    try_mount_bootsel_drive || true
    MOUNT="$(find_bootsel_mount || true)"
fi

if [[ -z "$MOUNT" ]]; then
    if try_enter_bootsel_serial; then
        wait_for_bootsel_mount || true
    else
        try_mount_bootsel_drive || true
        MOUNT="$(find_bootsel_mount || true)"
    fi
fi

if [[ -z "$MOUNT" ]]; then
    err "BOOTSEL upload failed"
    echo ""
    echo "  Instructions:"
    echo "  1. Make sure the board is in BOOTSEL mode"
    echo "  2. If RPI-RP2 appears in the file manager, mount it and run again"
    echo "  3. WHX is uploaded separately with: sudo $SCRIPT_DIR/upload-whx-picotool.sh"
    echo ""
    echo "  Mounted drives in /media/$USER/:"
    ls /media/"$USER"/ 2>/dev/null || echo "    (none)"
    echo "  Mounted drives in /run/media/$USER/:"
    ls /run/media/"$USER"/ 2>/dev/null || echo "    (none)"
    exit 1
fi

# Copy UF2
info "Copying to $MOUNT..."
cp "$UF2" "$MOUNT/"
sync

echo ""
ok "UF2 upload finished"
ok "File: $(basename "$UF2") -> $MOUNT/"
