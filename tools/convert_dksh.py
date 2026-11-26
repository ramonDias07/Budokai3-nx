import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

import deko3d_glsl


def bind_guest_texture_slots(text):
    # The converter numbers sampled-image expressions in encounter order.
    # RenderDk binds texture + sampler handles in Xbox fetch-register order.
    metadata = re.search(r" texb=([^\s]+)", text)
    if not metadata:
        return text
    entries = [tuple(map(int, item.split(":"))) for item in metadata[1].split(",")]
    by_slot = {}
    remap = {}
    for binding, texture_word, sampler_word in entries:
        slot = texture_word // 4 % 32
        if texture_word % 4 or texture_word >= 384 or slot >= 16:
            raise ValueError(f"unsupported B3 texture binding {binding}: {texture_word}")
        if sampler_word not in (384 + 4 * slot, 65535):
            raise ValueError(f"B3 texture {slot} uses a separate sampler: {sampler_word}")
        by_slot.setdefault(slot, []).append((binding, sampler_word))
        remap[binding] = slot

    # A textureSize-only alias shares the texture object with the sample.
    # Coalesce declarations after confirming that their sampler types agree.
    declarations = {}
    pattern = re.compile(r"^layout\(binding = (\d+)\) uniform (\w+) dk_tex_b\d+;\n", re.M)

    def declare(match):
        binding, sampler_type = int(match[1]), match[2]
        slot = remap[binding]
        if slot in declarations:
            if declarations[slot] != sampler_type:
                raise ValueError(f"conflicting sampler types for B3 texture {slot}")
            return ""
        declarations[slot] = sampler_type
        return f"layout(binding = {slot}) uniform {sampler_type} b3_guest_texture_{slot};\n"

    text = pattern.sub(declare, text)
    if declarations.keys() != by_slot.keys():
        raise ValueError("missing GLSL declarations for B3 texture slots")
    text = re.sub(r"\bdk_tex_b(\d+)\b", lambda m: f"b3_guest_texture_{remap[int(m[1])]}", text)
    mappings = []
    for slot, aliases in sorted(by_slot.items()):
        sampler_word = min(s for _, s in aliases)
        mappings.append(f"{slot}:{slot * 4}:{sampler_word}")
    return text.replace(metadata[0], " texb=" + ",".join(mappings), 1)


def convert_all(spv_dir, out_dir, spirv_cross, uam, workers=8, log=print):
    """Convert every *.spv in spv_dir to .glsl/.dksh in out_dir. Returns the number of shaders; raises on failure."""
    spv_dir, out_dir = pathlib.Path(spv_dir), pathlib.Path(out_dir)
    spirv_cross, uam = pathlib.Path(spirv_cross), pathlib.Path(uam)
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = sorted(spv_dir.glob("*.spv"))

    def convert(path):
        stage = "vert" if path.name.startswith("vs_") else "frag"
        with tempfile.TemporaryDirectory(dir=out_dir) as tmp:
            vk = pathlib.Path(tmp) / "shader.vk.glsl"
            glsl = pathlib.Path(tmp) / "shader.glsl"
            dksh = pathlib.Path(tmp) / "shader.dksh"
            result = subprocess.run([str(spirv_cross), str(path), "--version", "460", "--vulkan-semantics",
                                    "--relax-nan-checks", "--output", str(vk)], capture_output=True, text=True)
            if result.returncode:
                return path.name, "spirv-cross", (result.stderr or result.stdout).strip()[:300]
            try:
                text, _ = deko3d_glsl.convert(vk.read_text(encoding="utf-8"), stage,
                                              deko3d_glsl.DEFAULT_UBO_MAP, "handle")
                if stage == "frag":
                    text = bind_guest_texture_slots(text)
            except Exception as exc:
                return path.name, "glsl-convert", str(exc)[:300]
            glsl.write_text(text, encoding="utf-8", newline="\n")
            result = subprocess.run([str(uam), "-s", stage, "-o", str(dksh), str(glsl)], capture_output=True, text=True)
            if result.returncode or not dksh.is_file():
                output = (result.stdout + result.stderr).strip()
                return path.name, "uam", next((line.strip()[:300] for line in output.splitlines() if "error" in line.lower()), output[:300])
            out_dir.joinpath(path.stem + ".glsl").write_text(text, encoding="utf-8", newline="\n")
            out_dir.joinpath(path.stem + ".dksh").write_bytes(dksh.read_bytes())
            return path.name, "ok", str(dksh.stat().st_size)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(executor.map(convert, entries))
    counts = {}
    for _, status, _ in results:
        counts[status] = counts.get(status, 0) + 1
    log(f"shaders={len(results)} counts={counts}")
    for row in results:
        if row[1] != "ok":
            log("\t".join(row))
    if not entries or counts.get("ok", 0) != len(entries):
        raise RuntimeError(f"Shader conversion failed: {counts}")
    return len(results)


def main():
    try:
        convert_all(*sys.argv[1:5])
    except RuntimeError:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
