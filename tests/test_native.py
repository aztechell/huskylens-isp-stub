from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "third_party/minsdk/lib"


class NativeTests(unittest.TestCase):
    def compile_and_run(self, name: str, sources: list[Path], cube: bool = True,
                        stubs: str = "lcd_stubs") -> None:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="isp-native-") as directory:
            executable = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
            compiled = subprocess.run([
                compiler, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                *(["-Wno-unused-parameter", "-Wno-sign-compare"] if stubs == "sysctl_stubs" else []),
                f"-DISP_BOARD_CUBE={int(cube)}",
                "-I", str(ROOT / "tests" / stubs), "-I", str(ROOT / "src"),
                "-I", str(SDK / "drivers/include"),
                "-I", str(SDK / "bsp/include"),
                *(str(p) for p in sources), "-lm", "-o", str(executable),
            ], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            # Optional persistent PPM previews are drawn by the real C UI/font.
            arguments = []
            if name.startswith("ui") and os.environ.get("ISP_PREVIEW_DIR"):
                output = Path(os.environ["ISP_PREVIEW_DIR"])
                output.mkdir(parents=True, exist_ok=True)
                arguments.append(str(output.resolve()))
            result = subprocess.run([str(executable), *arguments], check=True,
                                    capture_output=True, text=True, timeout=10)
            self.assertIn("_OK", result.stdout)

    def test_actual_ui_on_both_displays(self) -> None:
        for cube in (False, True):
            with self.subTest(cube=cube):
                self.compile_and_run("ui", [ROOT / "tests/ui_harness.c",
                    ROOT / "src/ui.c", ROOT / "src/lcd_text.c"], cube=cube)

    def test_actual_cube_lcd_transport(self) -> None:
        self.compile_and_run("lcd", [ROOT / "tests/lcd_harness.c", ROOT / "src/lcd.c"])

    def test_actual_dma_register_setup_and_bounded_reset(self) -> None:
        self.compile_and_run("dma", [ROOT / "tests/dma_harness.c",
                                    SDK / "drivers/dmac_isp.c"])

    def test_actual_minimal_bsp_enables_dma_clock(self) -> None:
        self.compile_and_run("sysctl", [ROOT / "tests/sysctl_harness.c",
            SDK / "drivers/sysctl.c"], stubs="sysctl_stubs")


class ElfContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("isp_build", ROOT / "tools/build.py")
        cls.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.build)

    def validate(self, data: bytes) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stub.elf"
            path.write_bytes(data)
            self.build.validate_elf(path)

    @staticmethod
    def fixture() -> bytearray:
        data = bytearray(64 + 56 + 64)
        data[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<QQQ", data, 24, 0x80000000, 64, 120)
        struct.pack_into("<HHHH", data, 54, 56, 1, 64, 1)
        struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0x80000000,
                         0x80000000, 4096, 4096, 8)
        struct.pack_into("<IIQQQQIIQQ", data, 120, 0, 8, 2, 0x80001000,
                         0, 240 * 240 * 4, 0, 0, 64, 0)
        return data

    def test_sram_includes_dma_bss_and_stack_reservation(self) -> None:
        data = self.fixture()
        self.validate(data)
        struct.pack_into("<Q", data, 120 + 32, 6 * 1024 * 1024)
        with self.assertRaisesRegex(RuntimeError, "SRAM"):
            self.validate(data)

    def test_displaced_entry_and_load_are_rejected(self) -> None:
        for offset in (24, 64 + 24):
            data = self.fixture()
            struct.pack_into("<Q", data, offset, 0x80000004)
            with self.subTest(offset=offset), self.assertRaisesRegex(RuntimeError, "0x80000000"):
                self.validate(data)

    def test_cli_defaults_and_all_select_the_expected_builds(self) -> None:
        for options, expected in (([], ["huskylens"]),
                                  (["--board", "cube"], ["cube"]),
                                  (["--board", "all"], ["huskylens", "cube"])):
            with self.subTest(options=options), \
                 patch("sys.argv", ["build.py", *options]), \
                 patch.object(self.build, "find_toolchain", return_value=Path("compiler")), \
                 patch.object(self.build, "build") as build:
                self.assertEqual(self.build.main(), 0)
                self.assertEqual([call.args[0] for call in build.call_args_list], expected)


if __name__ == "__main__":
    unittest.main()
