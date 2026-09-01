#!/usr/bin/env python3
"""Build and run host-side tests for deferred Doom menu actions."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class MenuActionTests(unittest.TestCase):
    def compile_and_run(self, sources: list[Path], name: str) -> None:
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
                "-I",
                str(REPO_ROOT / "src"),
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

    def test_pre_wipe_frame_releases_deferred_game_action(self) -> None:
        self.compile_and_run(
            [
                REPO_ROOT / "tests" / "doom_pre_wipe_test.c",
                REPO_ROOT / "src" / "doom" / "doom_pre_wipe.c",
            ],
            "doom_pre_wipe_test",
        )

    def test_accept_and_back_control_confirmation_prompts(self) -> None:
        self.compile_and_run(
            [REPO_ROOT / "tests" / "doom_menu_action_test.c"],
            "doom_menu_action_test",
        )


if __name__ == "__main__":
    unittest.main()
