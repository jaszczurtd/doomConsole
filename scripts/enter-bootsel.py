#!/usr/bin/env python3
"""Request RP2040 BOOTSEL mode via the USB CDC 1200 bps touch convention."""

import argparse
import errno
import json
import os
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is not installed; cannot auto-enter BOOTSEL", file=sys.stderr)
    sys.exit(2)


PICO_USB_IDS = {
    "2e8a:0003",
    "2e8a:000a",
    "2e8a:000f",
    "2e8a:1020",
    "2e8a:103a",
    "2e8a:f00a",
    "2e8a:f00f",
}

DEBUG_PROBE_IDS = {
    "2e8a:0004",
    "2e8a:000c",
}


def usb_id_str(port_info):
    if port_info.vid is None or port_info.pid is None:
        return ""
    return f"{port_info.vid:04x}:{port_info.pid:04x}"


def is_serial_candidate(device):
    return device.startswith("/dev/ttyACM") or device.startswith("/dev/ttyUSB")


def read_preferred_port(settings_path):
    if not os.path.isfile(settings_path):
        return ""

    try:
        with open(settings_path, "r", encoding="utf-8") as f:
            settings = json.load(f)
    except Exception:
        return ""

    for key in ("persistentSerialMonitor.port", "arduino.uploadPort", "serial.port"):
        value = settings.get(key, "")
        if isinstance(value, str) and value.strip():
            return value.strip()

    return ""


def list_candidate_ports(preferred):
    ports = []
    fallback_ports = []

    if preferred and os.path.exists(preferred):
        ports.append(preferred)

    for port in list_ports.comports():
        if not is_serial_candidate(port.device):
            continue

        uid = usb_id_str(port)
        if uid in DEBUG_PROBE_IDS:
            continue

        if uid in PICO_USB_IDS or uid.startswith("2e8a:"):
            if port.device not in ports:
                ports.append(port.device)
        elif port.device not in ports and port.device not in fallback_ports:
            fallback_ports.append(port.device)

    return ports or fallback_ports


def is_disconnect_error(exc):
    if isinstance(exc, OSError) and exc.errno in (
        errno.EIO,
        errno.ENODEV,
        errno.ENOENT,
        errno.ENXIO,
        errno.EPROTO,
    ):
        return True

    text = str(exc).lower()
    return (
        "protocol error" in text
        or "input/output error" in text
        or "no such device" in text
        or "device disconnected" in text
    )


def touch_1200(port):
    ser = serial.Serial()
    ser.port = port
    ser.timeout = 0.1
    ser.write_timeout = 0.1
    ser.xonxoff = False
    ser.dsrdtr = False
    ser.rtscts = False

    ser.open()
    try:
        ser.baudrate = 9600
        ser.setDTR(True)
        time.sleep(0.1)
        ser.setDTR(False)
        ser.baudrate = 1200
    finally:
        try:
            ser.close()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="", help="Explicit serial port")
    parser.add_argument(
        "--settings",
        default="",
        help="VS Code settings.json path used to discover arduino.uploadPort",
    )
    args = parser.parse_args()

    preferred = args.port or read_preferred_port(args.settings)
    ports = list_candidate_ports(preferred)

    if not ports:
        print("No RP2040 USB CDC serial port found", file=sys.stderr)
        return 1

    last_error = None
    for port in ports:
        try:
            print(f"Requesting BOOTSEL via 1200 bps touch on {port}")
            touch_1200(port)
            return 0
        except Exception as exc:
            if is_disconnect_error(exc):
                print(
                    f"Port {port} disappeared during 1200 bps touch; "
                    "assuming BOOTSEL reset was requested"
                )
                return 0

            last_error = exc
            print(f"Failed to touch {port}: {exc}", file=sys.stderr)

    if last_error is not None:
        print(f"Could not request BOOTSEL: {last_error}", file=sys.stderr)

    return 1


if __name__ == "__main__":
    sys.exit(main())
