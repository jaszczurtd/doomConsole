#!/usr/bin/env python3
"""Build and run host-side tests for the Doom Bluetooth input adapter."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
JASZCZURHAL_ROOT = REPO_ROOT.parent / "libraries" / "JaszczurHAL"


class GamepadInputTests(unittest.TestCase):
    def compile_and_run(
        self,
        sources: list[Path],
        name: str,
        extra_defines: list[str] | None = None,
    ) -> None:
        candidates = [os.environ.get("CC"), "cc", "gcc", "clang"]
        compiler = next(
            (shutil.which(candidate) for candidate in candidates if candidate),
            None,
        )
        self.assertIsNotNone(compiler, "a C compiler is required")

        with tempfile.TemporaryDirectory(prefix=f"{name}-") as tmp:
            suffix = ".exe" if os.name == "nt" else ""
            executable = Path(tmp) / f"{name}{suffix}"
            command = [
                compiler,
                "-std=c17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DHAL_TARGET_MOCK=1",
                "-DHAL_ENABLE_BLUETOOTH_GAMEPAD=1",
                "-DHAL_ENABLE_KV=1",
                *(f"-D{define}" for define in (extra_defines or [])),
                "-I",
                str(REPO_ROOT / "tests" / "support"),
                "-I",
                str(REPO_ROOT),
                "-I",
                str(REPO_ROOT / "src"),
                "-I",
                str(JASZCZURHAL_ROOT / "src"),
                *(str(source) for source in sources),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
            self.assertEqual(
                compiled.returncode, 0, compiled.stdout + compiled.stderr
            )

            executed = subprocess.run(
                [str(executable)], check=False, capture_output=True, text=True
            )
            self.assertEqual(
                executed.returncode, 0, executed.stdout + executed.stderr
            )

    def test_adapter_state_machine_and_zero2_mapping(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_input_test",
            ["BT_AUTOMATIC_PAIRING=1"],
        )

    def test_manual_pairing_remains_available_when_automatic_mode_is_off(
        self,
    ) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_manual_pairing_test",
            ["BT_AUTOMATIC_PAIRING=0"],
        )

    def test_automatic_mode_reconnects_known_peer_without_opening_pairing(
        self,
    ) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_known_peer_test",
            ["BT_AUTOMATIC_PAIRING=1", "DOOM_TEST_START_KNOWN=1"],
        )

    def test_gamepad_remains_available_when_bond_storage_fails(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_storage_fallback_test",
            ["DOOM_TEST_BOND_STORAGE_FAIL=1"],
        )

    def test_failed_factory_reset_blocks_automatic_reconnect(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_failed_factory_reset_test",
            ["DOOM_TEST_FORGET_FAIL=1"],
        )

    def test_busy_factory_reset_is_retried(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_gamepad_input_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_gamepad_input.c",
            ],
            "doom_gamepad_busy_factory_reset_test",
            ["DOOM_TEST_FORGET_BUSY_ONCE=1"],
        )

    def test_gpio_and_gamepad_actions_are_merged(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_input_hal_test.c",
                REPO_ROOT / "src" / "jaszczurhal" / "doom_input_hal.c",
            ],
            "doom_input_hal_test",
        )


if __name__ == "__main__":
    unittest.main()
