#!/usr/bin/env bash
# =============================================================================
# Upload only the raw Doom WHX payload through picotool and verify it in flash.
# Put the board in BOOTSEL mode before running this script.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PAYLOAD_ADDR="${DOOM_WHD_FLASH_ADDR:-0x10200000}"
PICOTOOL="${PICOTOOL:-}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

bundled_picotool_candidates() {
    local sudo_home

    printf '%s\n' "$HOME/.arduino15/packages/rp2040/tools/pqt-picotool/4.1.0-1aec55e/picotool"

    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != "root" ]]; then
        sudo_home="$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6 || true)"
        if [[ -n "$sudo_home" ]]; then
            printf '%s\n' "$sudo_home/.arduino15/packages/rp2040/tools/pqt-picotool/4.1.0-1aec55e/picotool"
        fi
    fi
}

find_picotool() {
    local candidate

    if [[ -n "$PICOTOOL" && -x "$PICOTOOL" ]]; then
        return 0
    fi

    while IFS= read -r candidate; do
        if [[ -x "$candidate" ]]; then
            PICOTOOL="$candidate"
            return 0
        fi
    done < <(bundled_picotool_candidates)

    PICOTOOL="$(command -v picotool || true)"
    [[ -n "$PICOTOOL" && -x "$PICOTOOL" ]]
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [doom1.whx]

Uploads a raw WHX payload to flash and verifies it with picotool.

Environment:
  DOOM_WHD_FLASH_ADDR  Target flash/XIP address, default 0x10200000
  PICOTOOL             Path to picotool
  DOOM_UPLOAD_REBOOT   Set to 0 to skip reboot after verify
EOF
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
esac

PAYLOAD_RAW="${1:-$PROJECT_DIR/doom1.whx}"

if ! find_picotool; then
    err "picotool not found"
    err "Checked bundled Arduino paths for current user and SUDO_USER"
    exit 1
fi

if [[ ! -f "$PAYLOAD_RAW" ]]; then
    err "WHX payload not found: $PAYLOAD_RAW"
    exit 1
fi

info "picotool: $PICOTOOL"
info "payload:  $PAYLOAD_RAW"
info "address:  $PAYLOAD_ADDR"
info "Make sure the board is in BOOTSEL mode."

"$PICOTOOL" load --ignore-partitions -v "$PAYLOAD_RAW" -t bin -o "$PAYLOAD_ADDR"
"$PICOTOOL" verify "$PAYLOAD_RAW" -t bin -o "$PAYLOAD_ADDR"

if [[ "${DOOM_UPLOAD_REBOOT:-1}" != "0" && "${DOOM_UPLOAD_REBOOT:-1}" != "false" ]]; then
    "$PICOTOOL" reboot
fi

ok "WHX uploaded and verified at $PAYLOAD_ADDR"
