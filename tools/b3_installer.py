#!/usr/bin/env python3
"""B3deko installer: turns your own Xbox 360 disc image into the SD card folder for the Budokai 3 Switch port.

Usage: B3deko-Installer.exe                       (opens a window to pick your .iso)
       B3deko-Installer.exe --iso game.iso --out sd [--no-pause]
"""

import argparse
import ctypes
from ctypes import wintypes
import datetime
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import traceback

FROZEN = getattr(sys, "frozen", False)
if not FROZEN:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

import extract_game  # noqa: E402
import extract_iso  # noqa: E402
import shaders  # noqa: E402

NEEDED_BYTES = 3 * 1024 ** 3
OPTIONAL_FILES = ["B3.nro", "budokai3.toml", "LEIA-ME.txt", "traducao/ptbr.b3tr"]
LOG = None


class InstallError(Exception):
    """An error whose message is meant for the user."""


def say(text=""):
    print(text, flush=True)
    detail(text)


def detail(text):
    if LOG:
        LOG.write(f"{datetime.datetime.now():%H:%M:%S} {text}\n")
        LOG.flush()


def step(number, text):
    say("")
    say(f"{number}/4 {text}")


def pick_iso():
    """Windows 'Open file' dialog through comdlg32 (no tkinter needed)."""
    class OPENFILENAME(ctypes.Structure):
        _fields_ = [("lStructSize", wintypes.DWORD), ("hwndOwner", wintypes.HWND), ("hInstance", wintypes.HINSTANCE),
                    ("lpstrFilter", wintypes.LPCWSTR), ("lpstrCustomFilter", wintypes.LPWSTR),
                    ("nMaxCustFilter", wintypes.DWORD), ("nFilterIndex", wintypes.DWORD),
                    ("lpstrFile", wintypes.LPWSTR), ("nMaxFile", wintypes.DWORD), ("lpstrFileTitle", wintypes.LPWSTR),
                    ("nMaxFileTitle", wintypes.DWORD), ("lpstrInitialDir", wintypes.LPCWSTR),
                    ("lpstrTitle", wintypes.LPCWSTR), ("Flags", wintypes.DWORD), ("nFileOffset", wintypes.WORD),
                    ("nFileExtension", wintypes.WORD), ("lpstrDefExt", wintypes.LPCWSTR),
                    ("lCustData", wintypes.LPARAM), ("lpfnHook", ctypes.c_void_p),
                    ("lpTemplateName", wintypes.LPCWSTR), ("pvReserved", ctypes.c_void_p),
                    ("dwReserved", wintypes.DWORD), ("FlagsEx", wintypes.DWORD)]

    buffer = ctypes.create_unicode_buffer(32768)
    dialog = OPENFILENAME()
    dialog.lStructSize = ctypes.sizeof(OPENFILENAME)
    dialog.lpstrFilter = "Xbox 360 disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0\0"
    dialog.lpstrFile = ctypes.cast(buffer, wintypes.LPWSTR)
    dialog.nMaxFile = len(buffer)
    dialog.lpstrTitle = "Select your Dragon Ball Z Budokai HD Collection (Xbox 360, USA/Japan) .iso"
    dialog.Flags = 0x00001000 | 0x00000800 | 0x00080000 | 0x00000008  # file/path must exist, explorer, no CWD change
    if ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(dialog)):
        return Path(buffer.value)
    return None


def tool_paths(tools):
    paths = {
        "rexglue": tools / "bin" / "rexglue.exe",
        "xenosrecomp": tools / "bin" / "XenosRecomp.exe",
        "spirv_cross": tools / "bin" / "spirv-cross.exe",
        "uam": tools / "bin" / "uam.exe",
        "shader_common": tools / "data" / "shader_common.h",
        "layout": tools / "data" / "shader_layout.json",
        "manifest": tools / "app" / "dbz3_manifest.toml",
        "config": tools / "app" / "dbz3_config.toml",
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise InstallError("The installer is damaged (missing internal files). Download it again.")
    return paths


def existing_parent(path):
    path = path.resolve()
    while not path.exists():
        path = path.parent
    return path


def remove_tree(path):
    def writable(function, target, _):
        os.chmod(target, stat.S_IWRITE)
        function(target)
    if path.exists():
        shutil.rmtree(path, onerror=writable)


def run_tool(args, cwd, env, what):
    detail("run: " + " ".join(map(str, args)))
    result = subprocess.run(list(map(str, args)), cwd=cwd, env=env, capture_output=True, text=True, errors="replace")
    detail(result.stdout)
    detail(result.stderr)
    if result.returncode:
        raise InstallError(f"{what} failed (exit code {result.returncode}). See installer.log for details.")


def check_disc(iso, work, game_root):
    """Step 1: open the disc, find the game files and verify the executable. Returns the disc's file table."""
    if not iso.is_file():
        raise InstallError(f"The disc image was not found: {iso}")
    with iso.open("rb") as stream:
        try:
            base, kind = extract_iso.find_partition(stream)
            sector, size = extract_iso.read_volume_descriptor(stream, base)
            entries = {path.lower(): (path, entry) for path, entry in extract_iso.walk(stream, base, sector, size)
                       if not entry["is_dir"]}
        except extract_iso.ExtractError as error:
            raise InstallError(f"This file is not a readable Xbox 360 disc image ({error}). "
                               "Use a plain .iso (not CCI, GOD or ZAR).")
        required = {extract_game.XEX_PATH, *(f"dbz3/us/{name}" for name in extract_game.GAME_FILES)}
        if not required <= entries.keys():
            raise InstallError("This disc does not contain the expected game files. "
                               "Use the USA/Japan release of Dragon Ball Z Budokai HD Collection.")
        say(f"    Disc layout: {kind}")
        game_root.mkdir(parents=True, exist_ok=True)
        xex = game_root / "default.xex"
        extract_iso.extract_file(stream, base, entries[extract_game.XEX_PATH][1], xex)
    try:
        extract_game.validate_xex(xex)
    except extract_iso.ExtractError:
        raise InstallError("This is not the USA/Japan release of Dragon Ball Z Budokai HD Collection "
                           "(the game executable is different). The European disc is not supported.")
    say("    Disc accepted: USA/Japan release.")
    return base, entries


def extract_files(iso, base, entries, game_root):
    """Step 2: extract the twelve data files next to the executable."""
    names = sorted(extract_game.GAME_FILES)
    with iso.open("rb") as stream:
        for number, name in enumerate(names, 1):
            path, entry = entries[f"dbz3/us/{name}"]
            if base + entry["sector"] * extract_iso.SECTOR + entry["size"] > iso.stat().st_size:
                raise InstallError(f"The disc image is incomplete or damaged (cut off inside {path}).")
            say(f"    [{number}/{len(names)}] {path.rsplit('/', 1)[1]} ({entry['size'] / 1048576:.0f} MB)")
            extract_iso.extract_file(stream, base, entry, game_root / "us" / path.rsplit("/", 1)[1])


def decode_image(tools, work, env):
    """Decode the XEX with rexglue (decrypt + decompress) without generating any code."""
    app = work / "app"
    app.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(tools["manifest"], app / "dbz3_manifest.toml")
    shutil.copyfile(tools["config"], app / "dbz3_config.toml")
    image = work / "xex-image.bin"
    env = dict(env, REXGLUE_DUMP_IMAGE=str(image), REXGLUE_DUMP_ONLY="1")
    run_tool([tools["rexglue"], "codegen", "app/dbz3_manifest.toml", "--ignore-stamp"], work, env,
             "Decoding the game executable")
    if not image.is_file() or image.stat().st_size < 1 << 20:
        raise InstallError("Decoding the game executable did not produce a usable image. See installer.log.")
    return image


def build_shaders(tools, image, work, env):
    os.environ.update({"TMP": env["TMP"], "TEMP": env["TEMP"]})
    shaders.QUIET.update(capture_output=True, text=True, errors="replace")
    try:
        return shaders.build_shaders(image, work / "shaders", tools["xenosrecomp"], tools["shader_common"],
                                     tools["spirv_cross"], tools["uam"], layout=tools["layout"], log=detail)
    except subprocess.CalledProcessError as error:
        detail(str(error.stdout))
        detail(str(error.stderr))
        raise InstallError(f"A shader tool failed ({Path(error.cmd[0]).name}, exit code {error.returncode}). "
                           "See installer.log for details.")
    except ValueError as error:
        raise InstallError(f"The shader data does not match this game version: {error}")


def assemble(out, work, game_root, shader_dir, extras):
    package = out / "switch" / "B3deko"
    package.mkdir(parents=True, exist_ok=True)
    final_root = package / "game_root"
    remove_tree(final_root)
    shutil.move(str(game_root), str(final_root))
    for name in ("b3_shaders.nfsp", "shadercache_dksh.bin"):
        shutil.copyfile(shader_dir / name, package / name)
    for name in OPTIONAL_FILES:
        source = extras / name
        if source.is_file():
            (package / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, package / name)
        else:
            say(f"    Warning: {name} was not found next to the installer, so it was not copied.")
    return package


def install(iso, out, tools, extras):
    started = time.time()
    paths = tool_paths(tools)
    out = out.resolve()
    free = shutil.disk_usage(existing_parent(out)).free
    if free < NEEDED_BYTES:
        raise InstallError(f"Not enough free disk space on {existing_parent(out).drive or out}: "
                           f"{free / 1024 ** 3:.1f} GB free, about {NEEDED_BYTES / 1024 ** 3:.0f} GB needed.")
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="b3-installer-", dir=out))
    temp = work / "tmp"
    temp.mkdir()
    env = dict(os.environ, TMP=str(temp), TEMP=str(temp), TMPDIR=str(temp))
    game_root = work / "assets" / "game_root"
    try:
        step(1, "Checking your disc")
        say(f"    {iso}")
        base, entries = check_disc(iso, work, game_root)
        step(2, "Extracting the game files")
        extract_files(iso, base, entries, game_root)
        step(3, "Building the shaders")
        say("    Decoding the game executable...")
        image = decode_image(paths, work, env)
        say("    Converting the shaders (this takes a few minutes)...")
        shader_dir = build_shaders(paths, image, work, env)
        step(4, "Assembling the SD card folder")
        package = assemble(out, work, game_root, shader_dir, extras)
    finally:
        remove_tree(work)
    say("")
    say(f"Done in {(time.time() - started) / 60:.1f} minutes: {package}")
    say("Copy the 'switch' folder to the root of your SD card.")


def main():
    global LOG
    here = Path(sys.executable).parent if FROZEN else Path.cwd()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", type=Path, help="your Xbox 360 disc image (asks in a window when omitted)")
    parser.add_argument("--out", type=Path, default=here / "sd", help="output folder (default: 'sd' next to the installer)")
    parser.add_argument("--no-pause", action="store_true", help="do not wait for Enter at the end")
    parser.add_argument("--tools", type=Path, help="folder with the helper programs (not needed in the .exe)")
    parser.add_argument("--extras", type=Path, default=here, help="folder with B3.nro, budokai3.toml, LEIA-ME.txt, traducao")
    args = parser.parse_args()
    tools = Path(sys._MEIPASS) / "tools" if FROZEN else args.tools
    if tools is None:
        parser.error("--tools is required when running the script directly")

    try:
        LOG = open(here / "installer.log", "w", encoding="utf-8")
    except OSError:
        LOG = None
    code = 0
    try:
        say("B3deko installer (Dragon Ball Z Budokai 3 for Nintendo Switch)")
        iso = args.iso
        if iso is None:
            say("Select your Dragon Ball Z Budokai HD Collection .iso (Xbox 360, USA/Japan) in the window that opens.")
            iso = pick_iso()
            if iso is None:
                raise InstallError("No disc image was selected.")
        install(iso.resolve(), args.out, tools, args.extras)
    except InstallError as error:
        code = 1
        say("")
        say(f"ERROR: {error}")
        if LOG:
            say(f"Details were written to {here / 'installer.log'}")
    except (OSError, extract_iso.ExtractError) as error:
        code = 1
        detail(traceback.format_exc())
        say("")
        say(f"ERROR: {error}")
        say("Check that the disc image and the output folder are readable/writable and that the disk is not full.")
        if LOG:
            say(f"Details were written to {here / 'installer.log'}")
    except Exception as error:  # noqa: BLE001 - last resort, keep the window open
        code = 1
        detail(traceback.format_exc())
        say("")
        say(f"ERROR: unexpected problem ({type(error).__name__}: {error}).")
        if LOG:
            say(f"Details were written to {here / 'installer.log'}")
    if not args.no_pause:
        try:
            input("\nPress Enter to close this window...")
        except EOFError:
            pass
    sys.exit(code)


if __name__ == "__main__":
    main()
