#!/usr/bin/env python3
"""Recreate B3's Deko shader library from the locally decoded XEX image."""

import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess

import xxhash


ROOT = Path(__file__).resolve().parent
HEADER = struct.Struct(">III")
QUIET = {}  # the installer sets this to capture the helper programs console output
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


DOF_VARIANT = 0x10000000     # kDekoDofReduzido (sdk/include/rex/x360/peticion.h)
PROBE_VARIANT = 0x20000000   # kDekoProbeVertices (same file)
TAP = re.compile(r"texture\(b3_guest_texture_0, ((?:[^()]|\([^()]*\))*)\)\.xyz")


def reduced_dof(glsl):
    """The reduced depth-of-field blur: the center and the four full-radius axial taps of the game's nine."""
    taps = list(TAP.finditer(glsl))
    if len(taps) != 9:
        raise ValueError("The original blur shader no longer matches its nine-sample pattern")
    start = glsl.rindex("\n", 0, taps[0].start()) + 1
    end = glsl.index(";\n", taps[-1].end()) + 2
    statement = re.fullmatch(r"(    )vec3 (_\d+) = vec3\(.*\);\n", glsl[start:end], re.S)
    # The center tap reads at the coordinate of the depth fetch (the stock shader copies it into another vector).
    center = re.search(r"texture\(b3_guest_texture_1, ((?:[^()]|\([^()]*\))*)\)\.x\b", glsl)
    if not statement or not center:
        raise ValueError("The original blur shader no longer matches its nine-sample pattern")
    pad, name = statement[1], statement[2]
    axial = [f"texture(b3_guest_texture_0, {tap[1]}).xyz" for tap in taps[5:]]
    block = (f"{pad}// Keep the game's depth-dependent radius and center. Use the center\n"
             f"{pad}// and four full-radius axial taps, with weights that preserve DC gain.\n"
             f"{pad}vec3 {name} = (texture(b3_guest_texture_0, {center[1]}).xyz * 0.2\n"
             f"{pad}           + ({axial[0]}\n"
             f"{pad}           + {axial[1]}\n"
             f"{pad}           + {axial[2]}\n"
             f"{pad}           + {axial[3]}) * 0.2);\n")
    return glsl[:start] + block + glsl[end:]


def read_u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def vertex_probe(glsl, container):
    """A diagnostic copy of a vertex shader that also stores its first 8 vertices' exports (binding 13).

    The NRO draws it only when a vertex dump is requested (RenderDk::GravarSondaVertice); a missing probe only
    skips that dump. Export registers come from the container's interpolator table (usage in bits 4-7, index in
    bits 0-3, Xenos export register in bits 8-11).
    """
    header = read_u32(container, 24)
    table = header + 36 + 4 * (read_u32(container, header + 24) + read_u32(container, header + 28))
    dense_exports = {}
    semantics = {5: "TEXCOORD", 10: "COLOR", 11: "FOG", 3: "NORMAL", 6: "TANGENT", 7: "BINORMAL"}
    for index in range(read_u32(container, header + 32)):
        desc = read_u32(container, table + 4 * index)
        name = semantics.get((desc >> 4) & 15)
        output = f"out_var_{name}{desc & 15}"
        if name and f"out vec4 {output};" in glsl:
            dense_exports[(desc >> 8) & 15] = output
    # The clip-space export before the host's optional XY viewport transform: the else branch is the raw Xenos
    # export in its original XYZW order.
    positions = re.findall(r"else\s*\{\s*(_\d+) = (_\d+(?:\.\w+)?);\s*\}\s*gl_Position = \1;", glsl)
    version, body = glsl.split("\n", 1)
    if len(positions) != 1 or re.search(r"\breturn\s*;", glsl) or not body.rstrip().endswith("}"):
        raise ValueError("A probed vertex shader no longer has the expected shape")
    declaration = """
struct B3VertexProbe {
    uvec4 marker;
    vec4 exports[17];
};
layout(std430, binding = 13) buffer B3ProbeStorage { B3VertexProbe b3_probe[8]; };
"""
    lines = ["    uint b3_id = uint(gl_VertexID);",
             "    if (b3_id < 8u) {",
             f"        b3_probe[b3_id].exports[16] = {positions[0][1]};"]
    for register, name in sorted(dense_exports.items()):
        lines.append(f"        b3_probe[b3_id].exports[{register}] = {name};")
    mask = 0x10000 | sum(1 << reg for reg in dense_exports)
    lines += [f"        b3_probe[b3_id].marker = uvec4(0x42335653u, {mask}u, b3_id, 0u);", "    }", "}"]
    return version + "\n" + declaration + body.rstrip()[:-1] + "\n".join(lines) + "\n"


def write_cache(output, spirv_dir, dksh_dir, uam, shaders=None, probes=()):
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
    source.write_text(reduced_dof((dksh_dir / spv.with_suffix(".glsl").name).read_text(encoding="utf-8")),
                      encoding="utf-8", newline="\n")
    subprocess.run([uam, "-s", "frag", "-o", str(dof), str(source)], check=True, **QUIET)
    records[digest(spv.read_bytes()), 0, DOF_VARIANT] = dof.read_bytes()
    for key in probes:
        name = f"vs_{key:016X}"
        source = dksh_dir / f"{name}.probe.glsl"
        binary = dksh_dir / f"{name}.probe.dksh"
        source.write_text(vertex_probe((dksh_dir / f"{name}.glsl").read_text(encoding="utf-8"), shaders[key]),
                          encoding="utf-8", newline="\n")
        subprocess.run([uam, "-s", "vert", "-o", str(binary), str(source)], check=True, **QUIET)
        records[digest((spirv_dir / f"{name}.spv").read_bytes()), 0, PROBE_VARIANT] = binary.read_bytes()

    table = bytearray()
    data = bytearray()
    for (key, spec, variant), binary in sorted(records.items()):
        table += struct.pack("<QIIII", key, spec, variant, len(binary), 0)
        data += binary + b"\0" * (-len(binary) % 4)
    body = table + data
    # Must equal IdCompilador() in sdk/src/ui/deko3d/cache_dksh.cpp (with kRevisionGlsl from include/rex/ui/deko3d/glsl.h),
    # or the NRO rejects the whole cache ("made by another compiler"). Bump the number there and here together.
    compiler_id = digest(b"uam 1.1.0 + SwitchWakerHD 2d147aa (patches 1-7); SPIRV-Cross vulkan-sdk-1.4.363.0; GLSL revision 1")
    output.write_bytes(struct.pack("<4sIQIIQ", b"NFDK", 2, compiler_id, len(records), 0, digest(body)) + body)
    return len(records)


def build_shaders(image, output, xenosrecomp, shader_common, spirv_cross, uam, layout=None, log=print):
    """Build b3_shaders.nfsp and shadercache_dksh.bin in `output` from the decoded XEX image file."""
    import convert_dksh

    recipe = json.loads(Path(layout or ROOT / "shader_layout.json").read_text(encoding="utf-8"))
    shaders = containers(Path(image).read_bytes(), recipe)
    out = Path(output).resolve()
    # The shipped library was built over several XenosRecomp revisions, and the console-tested cache is keyed by
    # each SPIR-V module's hash. recipe["codegen"] names, per container, the translator switches of the revision
    # that produced its module (environment variables read by tools/xenos); the rest use the current defaults.
    codegen = recipe.get("codegen", {"profiles": {}, "shaders": {}})
    profile_of = {}
    for profile, keys in codegen["shaders"].items():
        for key in keys:
            if int(key, 16) not in shaders or int(key, 16) in profile_of:
                raise ValueError(f"Bad codegen entry for shader {key}")
            profile_of[int(key, 16)] = profile
    groups = {}
    for key, source in shaders.items():
        groups.setdefault(profile_of.get(key, "default"), {})[key] = source
    for profile, members in groups.items():
        inputs = out / "containers" / profile
        inputs.mkdir(parents=True, exist_ok=True)
        for key, source in members.items():
            (inputs / f"{key:016X}.bin").write_bytes(source)
    log(f"Prepared {len(shaders)} shader containers from the dump")
    spirv_dir = out / "spirv"
    dksh_dir = out / "dksh"
    spirv_dir.mkdir(exist_ok=True)
    xenosrecomp, shader_common, spirv_cross, uam = (Path(tool).resolve() for tool in
                                                     (xenosrecomp, shader_common, spirv_cross, uam))
    # Only the switches named here: a BANJO_* variable left in the caller's environment would change the output.
    base_env = {name: value for name, value in os.environ.items() if not name.upper().startswith("BANJO_")}
    for profile, members in sorted(groups.items()):
        hlsl_dir = out / "hlsl" / profile
        hlsl_dir.mkdir(parents=True, exist_ok=True)
        env = dict(base_env, BANJO_DEKO3D_SPIRV_DIR=str(spirv_dir), **codegen["profiles"].get(profile, {}))
        subprocess.run([str(xenosrecomp), str(out / "containers" / profile), str(out / f"shader_translations_{profile}.cpp"),
                        str(shader_common), str(hlsl_dir)], env=env, check=True, **QUIET)
    convert_dksh.convert_all(spirv_dir, dksh_dir, spirv_cross, uam, log=log)
    write_library(out / "b3_shaders.nfsp", shaders, spirv_dir)
    probes = [int(key, 16) for key in recipe.get("vertex_probes", [])]
    count = write_cache(out / "shadercache_dksh.bin", spirv_dir, dksh_dir, uam, shaders, probes)
    log(f"Built {len(shaders)} shader entries, with {count} precompiled Deko cache entries")
    return out


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
    if args.extract_only:
        recipe = json.loads((ROOT / "shader_layout.json").read_text(encoding="utf-8"))
        shaders = containers(args.image.read_bytes(), recipe)
        inputs = args.output.resolve() / "containers"
        inputs.mkdir(parents=True, exist_ok=True)
        for key, source in shaders.items():
            (inputs / f"{key:016X}.bin").write_bytes(source)
        print(f"Prepared {len(shaders)} shader containers from the dump", flush=True)
        return
    build_shaders(args.image, args.output, *tools, log=lambda text: print(text, flush=True))


if __name__ == "__main__":
    main()
