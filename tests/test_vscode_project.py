#!/usr/bin/env python3
"""Validate the generated cross-platform VS Code project contract."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
# One profile per assembly and variant of device_tree.json, each for its own
# ELF; ST7796S exists only on the RP2350 assembly.
RP2040 = ["interface/cmsis-dap.cfg", "target/rp2040.cfg"]
RP2350 = ["interface/cmsis-dap.cfg", "target/rp2350.cfg"]
DEBUG_PROFILES = {
    "Project: Debug pico2w (RP2350 ARM)": ("rp2350-arm/pico2w", RP2350),
    "Project: Debug pico2w variant: BOOT_PROBE (RP2350 ARM)":
        ("rp2350-arm/pico2w/variants/BOOT_PROBE", RP2350),
    "Project: Debug pico2w variant: ST7796S (RP2350 ARM)":
        ("rp2350-arm/pico2w/variants/ST7796S", RP2350),
    "Project: Debug rp2040-plus (RP2040)": ("rp2040/rp2040-plus", RP2040),
    "Project: Debug rp2040-plus variant: BOOT_PROBE (RP2040)":
        ("rp2040/rp2040-plus/variants/BOOT_PROBE", RP2040),
}


class VscodeProjectTests(unittest.TestCase):
    def test_generated_files_are_current(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "scripts" / "sync_vscode_project.py"),
                "--check",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tasks_have_unique_labels_and_windows_commands(self) -> None:
        document = json.loads(
            (REPO_ROOT / ".vscode" / "tasks.json").read_text(encoding="utf-8")
        )
        tasks = document["tasks"]
        labels = [task["label"] for task in tasks]
        self.assertEqual(len(labels), len(set(labels)))
        for task in tasks:
            self.assertIn("windows", task, task["label"])
            self.assertTrue(task["windows"].get("command"), task["label"])

        whx = next(task for task in tasks if task["label"] == "Project: Upload WHX Payload")
        self.assertEqual(whx["command"], "python3")
        self.assertEqual(whx["windows"]["command"], "py")
        self.assertIn("upload_whx.py", " ".join(whx["windows"]["args"]))

    def test_settings_and_json_contain_no_machine_local_paths(self) -> None:
        settings = json.loads(
            (REPO_ROOT / ".vscode" / "settings.json").read_text(encoding="utf-8")
        )
        self.assertNotIn("jaszczurhal.uploadPort", settings)
        self.assertEqual(
            settings["jaszczurhal.vscodeEntryWindows"],
            "../JaszczurHAL/vscode/entry/jh-vscode.cmd",
        )
        manifest = json.loads(
            (REPO_ROOT / ".vscode" / "jaszczurhal.project.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(manifest["identity"]["usbVid"], "0x2e8a")
        self.assertEqual(manifest["identity"]["usbPid"], "0x0009")
        # The tracked assembly is the Pico 2 W of device_tree.json.
        self.assertEqual(manifest["assembly"], "pico2w")
        tree = json.loads((REPO_ROOT / "device_tree.json").read_text(encoding="utf-8"))
        self.assertEqual(tree["assemblies"]["pico2w"]["target"], "rp2350-arm")
        self.assertEqual(tree["assemblies"]["pico2w"]["board"], "pico2w")
        for name in ("settings.json", "tasks.json", "keybindings.reference.json"):
            content = (REPO_ROOT / ".vscode" / name).read_text(encoding="utf-8")
            for forbidden in ("/dev/", "/home/", ".arduino15", "SUDO_USER"):
                self.assertNotIn(forbidden, content, name)

        launch = (REPO_ROOT / ".vscode" / "launch.json").read_text(encoding="utf-8")
        self.assertNotIn("${config:cortex-debug.", launch)
        profiles = {
            profile["name"]: profile
            for profile in json.loads(launch)["configurations"]
        }
        self.assertEqual(set(profiles), set(DEBUG_PROFILES))
        for name, (build, config_files) in DEBUG_PROFILES.items():
            self.assertEqual("${workspaceFolder}/.build/" + build + "/firmware.elf",
                             profiles[name]["executable"], name)
            self.assertEqual(config_files, profiles[name]["configFiles"], name)


if __name__ == "__main__":
    unittest.main()
