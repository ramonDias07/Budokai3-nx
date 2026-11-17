#!/usr/bin/env python3
"""Fetch the public SDK and prepare the dependencies for a local B3 build."""

from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SDK_COMMIT = "f961303fe3c6e3a90905b379ff4bf3de9a35a757"
THIRDPARTY = (
    "cli11", "libmspack", "FFmpeg", "tomlplusplus", "simde", "xxHash", "spdlog", "fmt", "utfcpp",
    "imgui", "sdl3", "o1heap", "vulkan-headers", "vulkan-memory-allocator", "spirv-headers",
    "spirv-tools", "glslang",
)


def git(*args, cwd=ROOT):
    subprocess.run(["git", *map(str, args)], cwd=cwd, check=True)


def prepare_sdk():
    sdk = ROOT / "sdk"
    if (sdk / ".b3-sdk-prepared").is_file():
        return
    if sdk.exists():
        raise SystemExit(f"Existing unprepared SDK directory: {sdk}")
    with tempfile.TemporaryDirectory(dir=ROOT, prefix="b3-sdk-") as folder:
        checkout = Path(folder) / "sdk"
        git("init", checkout)
        git("remote", "add", "origin", "https://github.com/nx-mod/rexglue-sdk-nx.git", cwd=checkout)
        git("fetch", "--depth", "1", "origin", SDK_COMMIT, cwd=checkout)
        git("checkout", "--detach", "FETCH_HEAD", cwd=checkout)
        git("submodule", "update", "--init", "--depth", "1", "--jobs", "4", "--",
            *(f"thirdparty/{name}" for name in THIRDPARTY), cwd=checkout)
        git("apply", "--ignore-space-change", ROOT / "patches/rexglue-nx.patch", cwd=checkout)
        git("apply", "--ignore-space-change", ROOT / "patches/ffmpeg-switch.patch",
            cwd=checkout / "thirdparty/FFmpeg")

        # Git for Windows can check symbolic links out as text files.
        mspack = checkout / "thirdparty/libmspack"
        for path in (mspack / "cabextract/mspack").iterdir():
            if path.is_file() and not path.is_symlink() and path.stat().st_size < 256:
                target = path.read_text(encoding="ascii", errors="replace").strip()
                if target.startswith("../../libmspack/mspack/"):
                    source = (path.parent / target).resolve(strict=True)
                    if not source.is_relative_to(mspack):
                        raise ValueError(f"Unexpected libmspack link target for {path.name}")
                    shutil.copyfile(source, path)

        git("clone", "--depth", "1", "--branch", "vulkan-sdk-1.4.304.0",
            "https://github.com/KhronosGroup/SPIRV-Cross.git", checkout / "thirdparty/SPIRV-Cross")
        shutil.copytree(ROOT / "tools/vendor/uam", checkout / "thirdparty/uam")
        (checkout / ".b3-sdk-prepared").write_text(SDK_COMMIT + "\n", encoding="ascii")
        checkout.rename(sdk)


if __name__ == "__main__":
    prepare_sdk()
