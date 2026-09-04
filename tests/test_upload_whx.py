#!/usr/bin/env python3
"""Tests for the cross-platform WHX picotool uploader."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import upload_whx  # noqa: E402


class UploadWhxTests(unittest.TestCase):
    def test_reserved_tail_is_read_from_project_configuration(self) -> None:
        self.assertEqual(upload_whx.configured_reserved_tail_size(), 8192)

    def test_build_commands_preserves_address_verify_and_reboot(self) -> None:
        picotool = Path("C:/managed tools/picotool.exe")
        payload = Path("C:/doom data/doom1.whx")

        commands = upload_whx.build_commands(picotool, payload)

        self.assertEqual(
            commands,
            [
                [
                    str(picotool),
                    "load",
                    "--ignore-partitions",
                    "-v",
                    str(payload),
                    "-t",
                    "bin",
                    "-o",
                    "0x10200000",
                ],
                [
                    str(picotool),
                    "verify",
                    str(payload),
                    "-t",
                    "bin",
                    "-o",
                    "0x10200000",
                ],
                [str(picotool), "reboot"],
            ],
        )

    def test_no_reboot_omits_only_the_reboot_command(self) -> None:
        commands = upload_whx.build_commands(
            Path("picotool"), Path("doom1.whx"), reboot=False
        )
        self.assertEqual([command[1] for command in commands], ["load", "verify"])

    def test_serial_selects_the_same_device_for_every_command(self) -> None:
        commands = upload_whx.build_commands(
            Path("picotool"), Path("doom1.whx"), serial="0123456789ABCDEF"
        )

        for command in commands:
            self.assertEqual(command[-2:], ["--ser", "0123456789ABCDEF"])

    def test_runner_stops_before_reboot_when_verify_fails(self) -> None:
        calls: list[list[str]] = []
        returncodes = iter((0, 7, 0))

        def runner(command: list[str]) -> int:
            calls.append(command)
            return next(returncodes)

        commands = upload_whx.build_commands(Path("picotool"), Path("doom1.whx"))
        result = upload_whx.execute_commands(commands, runner)

        self.assertEqual(result, 7)
        self.assertEqual([command[1] for command in calls], ["load", "verify"])

    def test_managed_host_environment_resolves_picotool_exe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            picotool = root / "managed picotool" / "picotool.exe"
            picotool.parent.mkdir()
            picotool.touch()
            state = root / "host-environment.json"
            state.write_text(
                json.dumps({"tools": {"picotool": str(picotool)}}),
                encoding="utf-8",
            )
            args = argparse.Namespace(
                picotool=None,
                host_environment=state,
                jaszczurhal_root=root / "JaszczurHAL",
            )

            resolved = upload_whx.resolve_picotool(args)

            self.assertEqual(resolved, picotool.resolve())

    def test_managed_jaszczurhal_build_resolves_picotool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "JaszczurHAL"
            executable = "picotool.exe" if sys.platform == "win32" else "picotool"
            picotool = root / ".build" / "tools" / "picotool" / executable
            picotool.parent.mkdir(parents=True)
            picotool.touch()
            args = argparse.Namespace(
                picotool=None,
                host_environment=None,
                jaszczurhal_root=root,
            )

            resolved = upload_whx.resolve_picotool(args)

            self.assertEqual(resolved, picotool.resolve())

    def test_payload_must_fit_declared_flash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            payload = Path(temporary) / "payload.whx"
            payload.write_bytes(b"x" * 17)

            with self.assertRaisesRegex(RuntimeError, "beyond"):
                upload_whx.validate_payload(
                    payload,
                    upload_whx.FLASH_XIP_BASE + 16,
                    32,
                    0,
                )

    def test_payload_must_not_overlap_persistent_storage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            payload = Path(temporary) / "payload.whx"
            payload.write_bytes(b"x" * 17)

            with self.assertRaisesRegex(RuntimeError, "reserved tail"):
                upload_whx.validate_payload(
                    payload,
                    upload_whx.FLASH_XIP_BASE + 32,
                    64,
                    16,
                )

    def test_dry_run_prints_commands_without_executing_picotool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = root / "payload.whx"
            payload.write_bytes(b"public fixture")
            picotool = root / "picotool.exe"
            picotool.write_text("must not execute\n", encoding="utf-8")

            result = subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "scripts" / "upload_whx.py"),
                    str(payload),
                    "--picotool",
                    str(picotool),
                    "--dry-run",
                    "--no-reboot",
                ],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"load"', result.stdout)
            self.assertIn('"verify"', result.stdout)
            self.assertNotIn('"reboot"', result.stdout)
            self.assertIn("USB was not accessed", result.stdout)
            self.assertIn("reserved: 8192 bytes", result.stdout)

    def test_dry_run_rejects_reservation_smaller_than_firmware(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = root / "payload.whx"
            payload.write_bytes(b"public fixture")
            picotool = root / "picotool"
            picotool.touch()

            result = subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "scripts" / "upload_whx.py"),
                    str(payload),
                    "--picotool",
                    str(picotool),
                    "--reserved-tail-size",
                    "4096",
                    "--dry-run",
                ],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("cannot be smaller", result.stderr)


if __name__ == "__main__":
    unittest.main()
