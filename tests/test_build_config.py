"""Check the Doom build configuration and what the recipe derives from it."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
JASZCZURHAL_ROOT = REPO_ROOT.parent / "libraries" / "JaszczurHAL"
sys.path.insert(0, str(JASZCZURHAL_ROOT / "scripts"))

import project_config  # noqa: E402


def build(target: str, variant: str | None = None) -> project_config.BuildConfig:
    targets = project_config.load_targets(JASZCZURHAL_ROOT)
    return project_config.evaluate_build(REPO_ROOT, targets[target], variant)


class BuildConfigTests(unittest.TestCase):
    def test_tft_clock_reaches_both_panel_families(self):
        for variant in (None, "ST7796S"):
            with self.subTest(variant=variant):
                config = build("rp2350-arm", variant).config
                self.assertEqual(config.integer("JH_ILI9341_SPI_DEFAULT_HZ"), 50000000)
                self.assertEqual(config.integer("JH_ST77XX_SPI_DEFAULT_HZ"), 50000000)

    def test_panel_selects_the_hal_display_driver(self):
        ili9341 = build("rp2350-arm").requested_features()
        st7796s = build("rp2350-arm", "ST7796S").requested_features()
        self.assertIn("HAL_ENABLE_ILI9341", ili9341)
        self.assertNotIn("HAL_ENABLE_ST7796S", ili9341)
        self.assertIn("HAL_ENABLE_ST7796S", st7796s)
        self.assertNotIn("HAL_ENABLE_ILI9341", st7796s)
        with self.assertRaises(project_config.ConfigurationRejected):
            build("rp2040", "ST7796S")

    def test_game_and_smoke_test_features(self):
        game = build("rp2350-arm").requested_features()
        probe = build("rp2350-arm", "BOOT_PROBE").requested_features()
        for feature in (
            "HAL_ENABLE_DMA_PWM_AUDIO",
            "HAL_ENABLE_APP_TASK1",
            "HAL_ENABLE_BLUETOOTH_GAMEPAD",
            "HAL_ENABLE_KV",
        ):
            self.assertIn(feature, game)
        self.assertNotIn("HAL_ENABLE_DMA_PWM_AUDIO", probe)
        self.assertNotIn("HAL_ENABLE_APP_TASK1", probe)
        self.assertNotIn("HAL_ENABLE_BLUETOOTH_GAMEPAD", build("rp2040").requested_features())

    def test_recipe_derives_screen_and_options(self):
        expected = {
            None: {"SCREENWIDTH=320", "SCREENHEIGHT=240", "DOOM_TFT_PANEL_ILI9341=1",
                   "DOOM_HIGHRES_SCENE=1", "DOOM_BOOT_PROBE_ONLY=0"},
            "ST7796S": {"SCREENWIDTH=480", "SCREENHEIGHT=320", "FIXED_SCREENWIDTH=0",
                        "DOOM_HIGHRES_SCENE=1"},
            "BOOT_PROBE": {"DOOM_BOOT_PROBE_ONLY=1"},
        }
        with tempfile.TemporaryDirectory(prefix="doom-recipe-") as tmp:
            source = Path(tmp)
            (source / "cmake").mkdir()
            (source / "board").mkdir()
            for name in ("definition", "reference"):
                (source / "board" / f"jh_link_contract_{name}.c").write_text(
                    "/* Build fixture. */\n", encoding="utf-8"
                )
            # Only configure the recipe: the SDK integration is a stub that
            # creates the HAL target the recipe extends.
            (source / "cmake" / "jh_rp_pico_sdk.cmake").write_text(
                'add_library(JaszczurHAL STATIC "${JH_ROOT}/board/jh_link_contract_definition.c")\n'
                'target_compile_definitions(JaszczurHAL PUBLIC ${EXTRA_HAL_DEFINES})\n'
                "function(jh_add_rp_pico_firmware name)\n"
                "endfunction()\n",
                encoding="utf-8",
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(doom_recipe C CXX)\n"
                'include("${JH_HAL_ROOT}/cmake/jh_project_config.cmake")\n'
                "jh_read_project_config(ROOT \"${JH_HAL_ROOT}\" CONFIG_DIR \"${JH_PROJECT_DIR}\"\n"
                "    TARGET rp2350-arm VARIANT \"${DOOM_TEST_VARIANT}\"\n"
                '    OUTPUT_DIR "${CMAKE_BINARY_DIR}/project")\n'
                'set(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")\n'
                "set(JH_TARGET rp2350-arm)\n"
                'set(JH_BOARD_GENERATED_DIR "${JH_ROOT}/board")\n'
                'set(JH_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}")\n'
                'include("${JH_PROJECT_DIR}/CMakeLists.txt")\n'
                "get_target_property(_firmware firmware COMPILE_DEFINITIONS)\n"
                "get_target_property(_hal JaszczurHAL INTERFACE_COMPILE_DEFINITIONS)\n"
                'file(WRITE "${CMAKE_BINARY_DIR}/definitions.txt" "${_firmware};${_hal}")\n',
                encoding="utf-8",
            )
            for variant, definitions in expected.items():
                with self.subTest(variant=variant):
                    binary = source / f"build-{variant or 'base'}"
                    result = subprocess.run(
                        ["cmake", "-S", str(source), "-B", str(binary),
                         f"-DJH_PROJECT_DIR:PATH={REPO_ROOT}",
                         f"-DJH_HAL_ROOT:PATH={JASZCZURHAL_ROOT}",
                         f"-DDOOM_TEST_VARIANT={variant or ''}"],
                        capture_output=True, text=True, timeout=120,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    actual = set(
                        (binary / "definitions.txt").read_text(encoding="utf-8").split(";")
                    )
                    self.assertTrue(definitions <= actual, sorted(definitions - actual))
                    if variant == "ST7796S":
                        self.assertIn("DOOM_TFT_PANEL_ST7796S=1", actual)
                        self.assertNotIn("DOOM_TFT_PANEL_ILI9341=1", actual)


if __name__ == "__main__":
    unittest.main()
