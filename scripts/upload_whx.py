#!/usr/bin/env python3
"""Load, verify and optionally reboot a Doom WHX payload with managed picotool."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable, Sequence


DEFAULT_ADDRESS = 0x10200000
FLASH_XIP_BASE = 0x10000000
DEFAULT_FLASH_SIZE = 0x00400000
Command = list[str]
Runner = Callable[[Sequence[str]], int | subprocess.CompletedProcess[object]]


def integer(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid integer: {value}") from error


def environment_reboot_default() -> bool:
    value = os.environ.get("DOOM_UPLOAD_REBOOT", "1").strip().lower()
    return value not in {"0", "false", "no", "off"}


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    project_dir = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "payload",
        type=Path,
        nargs="?",
        default=project_dir / "doom1.whx",
        help="Raw WHX payload (default: doom1.whx in the project root).",
    )
    parser.add_argument(
        "--address",
        type=integer,
        default=integer(os.environ.get("DOOM_WHD_FLASH_ADDR", hex(DEFAULT_ADDRESS))),
        help="Flash/XIP load address (default: 0x10200000).",
    )
    parser.add_argument(
        "--flash-size",
        type=integer,
        default=DEFAULT_FLASH_SIZE,
        help="Board flash capacity used for the safety check (default: 4 MiB).",
    )
    parser.add_argument("--picotool", type=Path, help="Explicit picotool executable.")
    parser.add_argument(
        "--host-environment",
        type=Path,
        help="JaszczurHAL host-environment.json containing managed picotool.",
    )
    parser.add_argument(
        "--jaszczurhal-root",
        type=Path,
        default=Path(os.environ.get("JASZCZURHAL_ROOT", project_dir.parent / "libraries" / "JaszczurHAL")),
        help="JaszczurHAL checkout used for managed-tool discovery.",
    )
    reboot = parser.add_mutually_exclusive_group()
    reboot.add_argument("--reboot", dest="reboot", action="store_true")
    reboot.add_argument("--no-reboot", dest="reboot", action="store_false")
    parser.set_defaults(reboot=environment_reboot_default())
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate inputs and print commands as JSON without accessing USB.",
    )
    return parser.parse_args(argv)


def read_managed_picotool(environment_file: Path) -> Path | None:
    if not environment_file.is_file():
        return None
    try:
        with environment_file.open("r", encoding="utf-8") as handle:
            state = json.load(handle)
    except (OSError, json.JSONDecodeError):
        return None
    value = state.get("tools", {}).get("picotool")
    return Path(value) if isinstance(value, str) and value else None


def resolve_picotool(args: argparse.Namespace) -> Path:
    candidates: list[Path] = []
    if args.picotool is not None:
        candidates.append(args.picotool)
    if os.environ.get("JH_PICOTOOL_EXECUTABLE"):
        candidates.append(Path(os.environ["JH_PICOTOOL_EXECUTABLE"]))
    if os.environ.get("PICOTOOL"):
        candidates.append(Path(os.environ["PICOTOOL"]))

    environment_files: list[Path] = []
    if args.host_environment is not None:
        environment_files.append(args.host_environment)
    if os.environ.get("JH_HOST_ENVIRONMENT"):
        environment_files.append(Path(os.environ["JH_HOST_ENVIRONMENT"]))
    environment_files.append(
        args.jaszczurhal_root / ".build" / "windows" / "host-environment.json"
    )
    for environment_file in environment_files:
        managed = read_managed_picotool(environment_file.expanduser().resolve())
        if managed is not None:
            candidates.append(managed)

    managed_name = "picotool.exe" if os.name == "nt" else "picotool"
    candidates.append(
        args.jaszczurhal_root / ".build" / "tools" / "picotool" / managed_name
    )

    for name in ("picotool", "picotool.exe"):
        discovered = shutil.which(name)
        if discovered:
            candidates.append(Path(discovered))

    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_file():
            return resolved
    raise RuntimeError(
        "picotool was not found; run JaszczurHAL setup or pass --picotool"
    )


def validate_payload(payload: Path, address: int, flash_size: int) -> None:
    if not payload.is_file():
        raise RuntimeError(f"WHX payload not found: {payload}")
    if address < FLASH_XIP_BASE:
        raise RuntimeError(
            f"load address {address:#x} is below flash XIP base {FLASH_XIP_BASE:#x}"
        )
    if flash_size <= 0:
        raise RuntimeError("flash size must be positive")
    payload_offset = address - FLASH_XIP_BASE
    payload_end = payload_offset + payload.stat().st_size
    if payload_end > flash_size:
        raise RuntimeError(
            f"payload ends at flash offset {payload_end:#x}, beyond "
            f"the declared {flash_size:#x}-byte flash"
        )


def build_commands(
    picotool: Path,
    payload: Path,
    address: int = DEFAULT_ADDRESS,
    *,
    reboot: bool = True,
) -> list[Command]:
    address_text = f"0x{address:08x}"
    commands = [
        [
            str(picotool),
            "load",
            "--ignore-partitions",
            "-v",
            str(payload),
            "-t",
            "bin",
            "-o",
            address_text,
        ],
        [
            str(picotool),
            "verify",
            str(payload),
            "-t",
            "bin",
            "-o",
            address_text,
        ],
    ]
    if reboot:
        commands.append([str(picotool), "reboot"])
    return commands


def execute_commands(commands: Sequence[Command], runner: Runner) -> int:
    for command in commands:
        result = runner(command)
        returncode = result if isinstance(result, int) else result.returncode
        if returncode != 0:
            return returncode
    return 0


def dry_run_runner(command: Sequence[str]) -> int:
    print(json.dumps(list(command)))
    return 0


def subprocess_runner(command: Sequence[str]) -> subprocess.CompletedProcess[object]:
    return subprocess.run(list(command), check=False)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    payload = args.payload.expanduser().resolve()
    try:
        picotool = resolve_picotool(args)
        validate_payload(payload, args.address, args.flash_size)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    digest = hashlib.sha256(payload.read_bytes()).hexdigest().upper()
    print(f"picotool: {picotool}")
    print(f"payload:  {payload} ({payload.stat().st_size} bytes, SHA-256 {digest})")
    print(f"address:  0x{args.address:08x}")
    print(f"reboot:   {'yes' if args.reboot else 'no'}")
    if not args.dry_run:
        print("The target board must already be in BOOTSEL mode.")

    commands = build_commands(picotool, payload, args.address, reboot=args.reboot)
    result = execute_commands(
        commands,
        dry_run_runner if args.dry_run else subprocess_runner,
    )
    if result != 0:
        print(f"error: picotool failed with exit code {result}", file=sys.stderr)
        return result
    if args.dry_run:
        print("Dry run completed; USB was not accessed.")
    else:
        print(f"WHX uploaded and verified at 0x{args.address:08x}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
