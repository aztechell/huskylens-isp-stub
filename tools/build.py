#!/usr/bin/env python3
"""Build board-specific HuskyLens and Maix Cube K210 ISP stubs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
MAX_STUB_SIZE = 64 * 1024
BOARDS = ("huskylens", "cube")
SDK_COMMIT = "02576ba67e8797444f3ee3f34c625b5ed048e707"
KTOOL_COMMIT = "0345aa90d9b3830641373fb4e3ce4edf45d0a46f"


def find_toolchain() -> Path | None:
    candidates = []
    if os.environ.get("KENDRYTE_TOOLCHAIN_BIN"):
        candidates.append(Path(os.environ["KENDRYTE_TOOLCHAIN_BIN"]))
    candidates.extend([
        ROOT / "_deps" / "kendryte-toolchain" / "bin",
        WORKSPACE / "hackylens" / "_deps" / "kendryte-toolchain" / "bin",
        WORKSPACE / "hackylens-legacy" / "_deps" / "kendryte-toolchain" / "bin",
    ])
    return next((p.resolve() for p in candidates
                 if (p / ("riscv64-unknown-elf-gcc.exe" if os.name == "nt"
                          else "riscv64-unknown-elf-gcc")).is_file()), None)


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def validate_elf(path: Path) -> None:
    """Reject a displaced entry/load address or SRAM/stack overflow."""
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError("expected a little-endian ELF64 executable")
    entry, phoff, shoff = struct.unpack_from("<QQQ", data, 24)
    phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 54)
    loads = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
             for i in range(phnum)]
    loads = [p for p in loads if p[0] == 1 and p[5] != 0]
    if entry != 0x80000000 or not loads or min(p[4] for p in loads) != entry:
        raise RuntimeError("ELF entry and first load address must be 0x80000000")
    end = entry
    for i in range(shnum):
        section = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize)
        if section[2] & 2 and section[5]:
            if section[3] < entry:
                raise RuntimeError("allocated section is outside cached SRAM")
            end = max(end, section[3] + section[5])
    if ((end + 63) & ~63) + 2 * 32768 > 0x80600000:
        raise RuntimeError("ELF allocations and two stacks exceed 6 MiB SRAM")


def build(board: str, args: argparse.Namespace, toolchain: Path) -> None:
    suffix = "_headless" if args.headless else "_lcd_init" if args.lcd_init_only else ""
    work = (ROOT / "build" / f"{board}{suffix}").resolve()
    # All recursive cleanup is confined to this board's generated build tree.
    if not work.is_relative_to((ROOT / "build").resolve()):
        raise RuntimeError("unsafe build directory")
    if work.exists():
        shutil.rmtree(work)
    sdk = work / "minsdk"
    shutil.copytree(ROOT / "third_party" / "minsdk", sdk,
                    ignore=shutil.ignore_patterns("src"))

    stage = sdk / "src" / "isp_stub"
    shutil.copytree(ROOT / "src", stage)

    build_dir = work / "sdk"
    build_dir.mkdir(parents=True)
    cmake = shutil.which("cmake") or r"C:\Program Files\CMake\bin\cmake.exe"
    generator = "MinGW Makefiles" if os.name == "nt" else "Ninja"
    slash = lambda p: str(p).replace("\\", "/")
    run([cmake, "-S", slash(sdk), "-B", slash(build_dir), "-G", generator,
         "-DPROJ=isp_stub", f"-DTOOLCHAIN={slash(toolchain)}",
         f"-DISP_BOARD={board}",
         f"-DISP_HEADLESS={'ON' if args.headless else 'OFF'}",
         f"-DISP_LCD_INIT_ONLY={'ON' if args.lcd_init_only else 'OFF'}",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"])
    run([cmake, "--build", slash(build_dir), "--parallel"])

    built_bin = build_dir / "isp_stub.bin"
    built_elf = build_dir / "isp_stub"
    built_map = build_dir / "isp_stub.map"
    for path in (built_bin, built_elf, built_map):
        if not path.is_file():
            raise RuntimeError(f"missing build output: {path}")
    if built_bin.stat().st_size > MAX_STUB_SIZE:
        raise RuntimeError(f"stub is {built_bin.stat().st_size} bytes; limit is {MAX_STUB_SIZE}")
    validate_elf(built_elf)

    output = ROOT / "build"
    output_bin = output / f"isp_prog_{board}{suffix}.bin"
    shutil.copy2(built_bin, output_bin)
    output_bin.chmod(0o644)
    elf_name = "isp_stub" if board == "huskylens" else "isp_stub_cube"
    shutil.copy2(built_elf, output / f"{elf_name}{suffix}.elf")
    shutil.copy2(built_map, output / f"{elf_name}{suffix}.map")
    digest = hashlib.sha256(built_bin.read_bytes()).hexdigest()
    if not args.headless and not args.lcd_init_only:
        release_bin = ROOT / f"isp_prog_{board}.bin"
        shutil.copy2(built_bin, release_bin)
        release_bin.chmod(0o644)
        manifest_path = ROOT / "artifacts.json"
        manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
        manifest[board] = {"file": release_bin.name,
                           "size": built_bin.stat().st_size, "sha256": digest}
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"[OK] {board}: {built_bin.stat().st_size} bytes; ELF/SRAM verified")
    print(f"[OK] sha256 {digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=(*BOARDS, "all"), default="huskylens")
    diagnostic = parser.add_mutually_exclusive_group()
    diagnostic.add_argument("--headless", action="store_true",
                            help="build a diagnostic stub with LCD calls disabled")
    diagnostic.add_argument("--lcd-init-only", action="store_true",
                            help="configure LCD hardware but skip panel initialization/drawing")
    args = parser.parse_args()
    toolchain = find_toolchain()
    if not (ROOT / "third_party/minsdk/CMakeLists.txt").is_file() or not toolchain:
        print("Dependencies missing; run tools/bootstrap_deps.py", file=sys.stderr)
        return 1
    for board in BOARDS if args.board == "all" else (args.board,):
        build(board, args, toolchain)
    print(f"[INFO] expected SDK commit {SDK_COMMIT}")
    print(f"[INFO] minimal BSP derived from ktool {KTOOL_COMMIT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
