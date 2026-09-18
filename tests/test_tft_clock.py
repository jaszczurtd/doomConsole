"""Exercise the Doom CMake recipe with a small HAL driver build."""

from pathlib import Path
import subprocess
import tempfile
import unittest


class TftClockTests(unittest.TestCase):
    def test_requested_clock_reaches_hal_and_application(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="doom-tft-clock-") as tmp:
            source = Path(tmp)
            (source / "cmake").mkdir()
            (source / "board").mkdir()
            for name in ("definition", "reference"):
                (source / "board" / f"jh_link_contract_{name}.c").write_text(
                    "/* Build fixture. */\n", encoding="utf-8"
                )
            (source / "clock.c").write_text(
                '_Static_assert(JH_ILI9341_SPI_DEFAULT_HZ == 50000000, "ILI9341 clock");\n'
                '_Static_assert(JH_ST77XX_SPI_DEFAULT_HZ == 50000000, "ST77xx clock");\n'
                'int main(void) { return 0; }\n', encoding="utf-8"
            )
            # The SDK integration consumes EXTRA_HAL_DEFINES when it creates
            # the library and publishes those definitions to its consumers.
            (source / "cmake" / "jh_rp_pico_sdk.cmake").write_text(
                'add_library(JaszczurHAL STATIC "${JH_ROOT}/clock.c")\n'
                'target_compile_definitions(JaszczurHAL PUBLIC ${EXTRA_HAL_DEFINES})\n'
                'function(jh_add_rp_pico_firmware name)\n'
                '  target_link_libraries(${name} PRIVATE JaszczurHAL)\n'
                'endfunction()\n', encoding="utf-8"
            )
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(doom_tft_clock C CXX)\n'
                'set(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")\n'
                'set(JH_BOARD_GENERATED_DIR "${JH_ROOT}/board")\n'
                'set(JH_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}")\n'
                'include("${JH_PROJECT_DIR}/CMakeLists.txt")\n'
                'add_executable(clock_consumer "${JH_ROOT}/clock.c")\n'
                'target_link_libraries(clock_consumer PRIVATE JaszczurHAL)\n',
                encoding="utf-8"
            )
            for panel in ("ili9341", "st7796s"):
                with self.subTest(panel=panel):
                    build = source / panel
                    for command in (
                        ["cmake", "-S", str(source), "-B", str(build),
                         f"-DJH_PROJECT_DIR:PATH={root}", "-DJH_TARGET=rp2350-arm",
                         f"-DDOOM_TFT_PANEL={panel}", "-DDOOM_HIGHRES_SCENE=1",
                         "-DJH_ILI9341_SPI_DEFAULT_HZ=50000000"],
                        ["cmake", "--build", str(build), "--target", "clock_consumer"],
                    ):
                        result = subprocess.run(command, capture_output=True,
                                                text=True, timeout=60)
                        self.assertEqual(result.returncode, 0,
                                         result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
