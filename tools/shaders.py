#!/usr/bin/env python3
"""Recreate B3's Deko shader library from the locally decoded XEX image."""

import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

import xxhash


ROOT = Path(__file__).resolve().parent
HEADER = struct.Struct(">III")
MAGIC = b"NFSSPV\0\0"


def digest(data):
    return xxhash.xxh3_64_intdigest(data)


def containers(image, recipe):
    wanted = {int(value, 16) for value in recipe["originals"]}
    found = {}
    for match in re.finditer(rb"\x10\x2a\x11[\x00\x01]", image):
        begin = match.start()
        if begin + HEADER.size > len(image):
            continue
        _, header_bytes, code_bytes = HEADER.unpack_from(image, begin)
        length = header_bytes + code_bytes
        if length < 56 or length > 65536 or length % 4 or begin + length > len(image):
            continue
        source = image[begin:begin + length]
        key = digest(source)
        if key in wanted:
            found[key] = source
    missing = wanted - found.keys()
    if missing:
        raise ValueError(f"Unsupported XEX image: {len(missing)} required shader containers are missing")

    for name, patch in recipe["variants"].items():
        template = found[int(patch["template"], 16)]
        words = tuple(item[0] for item in struct.iter_unpack(">I", template))
        variant = [word for start, count in patch["slices"] for word in words[start:start + count]]
        for index, mask in patch["xor"]:
            variant[index] ^= int(mask, 16)
        source = struct.pack(f">{len(variant)}I", *variant)
        if digest(source) != int(name, 16):
            raise ValueError(f"Shader variant {name} does not match its source recipe")
        found[int(name, 16)] = source
    return found


def write_library(output, shaders, spirv_dir):
    body = bytearray()
    for key, source in sorted(shaders.items()):
        stage = "vs" if source[3] == 1 else "ps"
        spirv = (spirv_dir / f"{stage}_{key:016X}.spv").read_bytes()
        if not spirv.startswith(b"\x03\x02\x23\x07") or len(spirv) % 4:
            raise ValueError(f"Invalid SPIR-V module for {key:016X}")
        body += struct.pack("<IIQ", len(source), len(spirv) // 4, key) + source + spirv
    output.write_bytes(MAGIC + struct.pack("<IIQ", 1, len(shaders), digest(body)) + body)


def write_cache(output, spirv_dir, dksh_dir, uam):
    records = {}
    for path in sorted(spirv_dir.glob("*.spv")):
        stage = path.stem.split("_", 1)[0]
        key = (digest(path.read_bytes()), 0, 0xFFFFFFFF if stage == "vs" else 0)
        binary = (dksh_dir / path.with_suffix(".dksh").name).read_bytes()
        if not binary.startswith(b"DKSH") or records.setdefault(key, binary) != binary:
            raise ValueError(f"Invalid or conflicting shader in the cache: {path.name}")
    spv = spirv_dir / "ps_63E1987853442D42.spv"
    dof = dksh_dir / "ps_dof_leve.dksh"
    source = dksh_dir / "ps_dof_leve.glsl"
    original = (dksh_dir / spv.with_suffix(".glsl").name).read_text(encoding="utf-8")
    taps = list(re.finditer(r"texture\(b3_guest_texture_0, (?:[^()]|\([^()]*\))*\)\.xyz", original))
    if len(taps) != 9:
        raise ValueError("The original blur shader no longer matches its nine-sample pattern")
    # Keep the center and four full-radius axial samples. The stock shader
    # divides the sum by nine, so scale the five retained samples by 9/5.
    for index, tap in reversed(list(enumerate(taps))):
        replacement = f"(1.8 * {tap[0]})" if index in (0, 5, 6, 7, 8) else "vec3(0.0)"
        original = original[:tap.start()] + replacement + original[tap.end():]
    source.write_text(original, encoding="utf-8")
    subprocess.run([uam, "-s", "frag", "-o", str(dof), str(source)], check=True)
    records[digest(spv.read_bytes()), 0, 0x10000000] = dof.read_bytes()

    table = bytearray()
    data = bytearray()
    for (key, spec, variant), binary in sorted(records.items()):
        table += struct.pack("<QIIII", key, spec, variant, len(binary), 0)
        data += binary + b"\0" * (-len(binary) % 4)
    body = table + data
    compiler_id = digest(b"Budokai3-nx local build")
    output.write_bytes(struct.pack("<4sIQIIQ", b"NFDK", 2, compiler_id, len(records), 0, digest(body)) + body)
    return len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="decoded image produced by ReXGlue's REXGLUE_DUMP_IMAGE")
    parser.add_argument("output", type=Path)
    parser.add_argument("--extract-only", action="store_true")
    parser.add_argument("--xenosrecomp", type=Path)
    parser.add_argument("--shader-common", type=Path)
    parser.add_argument("--spirv-cross", type=Path)
    parser.add_argument("--uam", type=Path)
    args = parser.parse_args()
    tools = [args.xenosrecomp, args.shader_common, args.spirv_cross, args.uam]
    if not args.extract_only and any(tool is None or not tool.is_file() for tool in tools):
        parser.error("the four shader tools/headers must be provided for a shader build")
    recipe = json.loads((ROOT / "shader_layout.json").read_text(encoding="utf-8"))
    shaders = containers(args.image.read_bytes(), recipe)
    out = args.output.resolve()
    inputs = out / "containers"
    inputs.mkdir(parents=True, exist_ok=True)
    for key, source in shaders.items():
        (inputs / f"{key:016X}.bin").write_bytes(source)
    print(f"Prepared {len(shaders)} shader containers from the dump", flush=True)
    if args.extract_only:
        return

    xenosrecomp, shader_common, spirv_cross, uam = (str(tool.resolve()) for tool in tools)
    spirv_dir = out / "spirv"
    hlsl_dir = out / "hlsl"
    dksh_dir = out / "dksh"
    spirv_dir.mkdir(exist_ok=True)
    hlsl_dir.mkdir(exist_ok=True)
    env = dict(os.environ, BANJO_DEKO3D_SPIRV_DIR=str(spirv_dir))
    subprocess.run([xenosrecomp, str(inputs), str(out / "shader_translations.cpp"), shader_common, str(hlsl_dir)],
                   env=env, check=True)
    subprocess.run([sys.executable, str(ROOT / "convert_dksh.py"), str(spirv_dir), str(dksh_dir), spirv_cross, uam],
                   check=True)
    write_library(out / "b3_shaders.nfsp", shaders, spirv_dir)
    count = write_cache(out / "shadercache_dksh.bin", spirv_dir, dksh_dir, uam)
    print(f"Built {len(shaders)} shader entries, with {count} precompiled Deko cache entries")


if __name__ == "__main__":
    main()
