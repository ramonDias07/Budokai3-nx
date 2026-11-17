#!/usr/bin/env python3
"""Generate a local Nintendo Switch package from a personal Budokai HD Collection dump."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from extract_game import extract_game


ROOT = Path(__file__).resolve().parents[1]


def run(args, env):
    subprocess.run(list(map(str, args)), cwd=ROOT, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    for program in ("rexglue", "xenosrecomp", "spirv-cross", "uam", "devkitpro"):
        parser.add_argument(f"--{program}", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=ROOT / "sdk")
    parser.add_argument("--icon", type=Path, help="Optional 256 × 256 JPEG for your local NRO")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 6))
    args = parser.parse_args()

    out = ROOT / "out"
    temporary = out / "temp"
    temporary.mkdir(parents=True, exist_ok=True)
    devkitpro = args.devkitpro.resolve()
    env = dict(os.environ, TMP=str(temporary), TEMP=str(temporary), TMPDIR=str(temporary),
               DEVKITPRO=str(devkitpro), DEVKITA64=str(devkitpro / "devkitA64"))
    env["PATH"] = os.pathsep.join([str(devkitpro / "devkitA64/bin"), str(devkitpro / "tools/bin"), env["PATH"]])

    game_root = ROOT / "assets/game_root"
    extract_game(args.iso.resolve(), game_root)
    decoded_image = out / "xex-image.bin"
    decoded_image.unlink(missing_ok=True)
    env["REXGLUE_DUMP_IMAGE"] = str(decoded_image)
    run([args.rexglue.resolve(), "codegen", "app/dbz3_manifest.toml", "--ignore-stamp"], env)
    if not decoded_image.is_file():
        raise SystemExit("The ReXGlue build did not export the decoded XEX. Use the SDK build described in docs/build.md.")

    shader_out = out / "shaders"
    run([sys.executable, ROOT / "tools/shaders.py", decoded_image, shader_out,
         "--xenosrecomp", args.xenosrecomp.resolve(), "--shader-common", ROOT / "tools/xenos/shader_common.h",
         "--spirv-cross", args.spirv_cross.resolve(), "--uam", args.uam.resolve()], env)

    build_dir = out / "switch"
    options = [f"-DBUDOKAI_ICON={args.icon.resolve()}"] if args.icon else []
    run(["cmake", "-S", ROOT, "-B", build_dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'tools/switch.cmake'}", f"-DDEVKITPRO={devkitpro}",
         f"-DREXSDK_DIR={args.sdk.resolve()}", *options], env)
    run(["cmake", "--build", build_dir, "--target", "budokai3_nx", "--parallel", args.jobs], env)

    package = out / "sd/switch/B3deko"
    package.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build_dir / "budokai3_nx.nro", package / "B3.nro")
    shutil.copyfile(ROOT / "app/budokai3.toml", package / "budokai3.toml")
    for name in ("b3_shaders.nfsp", "shadercache_dksh.bin"):
        shutil.copyfile(shader_out / name, package / name)
    shutil.copytree(game_root, package / "game_root", dirs_exist_ok=True)
    print(f"Copy {package} to the Switch SD card under /switch/.")


if __name__ == "__main__":
    main()
