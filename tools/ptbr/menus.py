"""Steps 3-4: translated menu textures, #AMB menu containers, #ACA animations and DATA_CMN signs.

Base container: the 360 data_spn.afs entry. The translators worked on the Spanish slot of the PS2
game and kept (or changed) the Spanish texture sizes, so the Spanish 360 layout is the one that
matches the translated images. Inside a container only the textures the translation changed are
replaced; every other texture and every non-texture block stays byte-identical to data_spn.afs.

Per changed texture (PS2 translated image `tr`, PS2 clean Spanish image `cl`, 360 template `t`):
  same     t size == tr size: encoded in the template's format (DXT3, 32-bit or 16-bit A4R4G4B4).
  resized  t size == cl size != tr size (the translators resized it; the 360 kept the PS2 size):
           kept at the translated size; the texture header and DDS are resized (the game takes the
           size from the texture header, see x360tex).
  upscale  t size != cl size (the 360 redrew it at about 1.5x): Real-ESRGAN to the 360 size, or to
           tr size x (t / cl) when the translators also resized it. Lanczos fallback when the
           quality guard trips (listed in the report).
  crop     same width, the 360 texture shorter (the 360 cut a long list): top rows of the image.
  missing  the PS2 texture has no 360 counterpart (the 360 container has fewer textures): skipped.
A few full-screen backgrounds and frames keep the 360 texture (KEEP_360): the 360 redrew them in HD.
"""

import struct
from dataclasses import dataclass, field

import numpy as np

from . import afs, amb, ps2tex, x360tex

AMB_LAST = 52                       # entries 0..52 of DATA_SPN are #AMB containers
ACA_FROM_SPN = (621, 2040, 2044)    # language-specific #ACA that animate translated textures
CMN_SIGNS = (3787, 3822, 3848)      # DATA_CMN stage textures with translated signs (same size)


@dataclass
class TexJob:
    file: str            # "data_usi" or "data_cmn"
    entry: int
    block: int           # #AMB block index, -1 for a top-level #AZT
    tex: int
    kind: str
    source: np.ndarray   # translated PS2 image (tr size)
    target: tuple        # (w, h) of the final image
    template: object     # x360tex.Texture
    ps2_log2: tuple
    keys: tuple = None   # upscaler keys
    ps2_index: int = -1
    match: float = 0.0
    method: str = ""
    error: float = 0.0
    result: np.ndarray = None


@dataclass
class Container:
    file: str
    entry: int
    kind: str            # "amb", "azt", "aca"
    base: bytes          # decompressed 360 entry used as base
    blocks: dict = field(default_factory=dict)   # block index -> x360tex.Azt (parsed, to patch)
    jobs: list = field(default_factory=list)
    missing: list = field(default_factory=list)      # changed PS2 textures with no 360 counterpart
    unmatched: list = field(default_factory=list)    # 360 textures whose PS2 twin changed but the pictures differ
    remapped: list = field(default_factory=list)     # (block, 360 index, PS2 index, error)
    backgrounds: list = field(default_factory=list)  # translated full-screen art kept as on the 360
    output: bytes = None


def _texture_changed(tr_data, p, cl_data, q):
    if (p.width, p.height) != (q.width, q.height):
        return True
    try:
        return ps2tex.decode(tr_data, p).tobytes() != ps2tex.decode(cl_data, q).tobytes()
    except ps2tex.AmtError:
        return True


def _near(a, b, tolerance=0.02):
    return abs(a - b) <= tolerance * max(a, b)


def plan_texture(file, entry, block, j, tr_data, p, q, template, clean_image=None):
    tr_size, cl_size = (p.width, p.height), (q.width, q.height)
    t_size = (template.width, template.height)
    source = ps2tex.decode(tr_data, p)
    log2 = (p.log2_width, p.log2_height)
    if t_size == tr_size:
        kind, target = "same", t_size
    elif t_size == cl_size:
        kind, target = "resized", tr_size
    else:
        sx, sy = t_size[0] / cl_size[0], t_size[1] / cl_size[1]
        if _near(sx, 1.0) and sy < 1 and tr_size[0] == t_size[0]:
            kind, target = "crop", t_size
            source = source[:t_size[1]]
        else:
            kind = "upscale"
            target = t_size if tr_size == cl_size else (max(1, round(tr_size[0] * sx)), max(1, round(tr_size[1] * sy)))
    return TexJob(file, entry, block, j, kind, source, target, template, log2)


REMAP_RATIO = 0.5    # another PS2 texture must match at least twice as well as the same index
REMAP_MAX = 12.0     # ... and match well in absolute terms (thumbnail error, premultiplied 0-255)
DIFFERS = 20.0       # above this the 360 picture shows other wording than the PS2 one (reported)


def _compatible(cl_size, t_size):
    (cw, ch), (tw, th) = cl_size, t_size
    if tw == cw and th <= ch:
        return True
    aspect = (tw / th) / (cw / ch)
    return 0.8 <= aspect <= 1.25 and 0.9 <= tw / cw <= 2.1


def _thumb(image, crop_height=None):
    """Small premultiplied version of an image: tolerant to the small shifts of the 360 redraws."""
    from PIL import Image
    from .upscale import premultiplied
    if crop_height is not None:
        image = image[:crop_height]
    pm = premultiplied(image).astype(np.float32)
    return np.stack([np.asarray(Image.fromarray(pm[..., c], mode="F").resize((48, 16), Image.BOX))
                     for c in range(4)], axis=-1)


def match_error(clean_image, template_image):
    th, tw = template_image.shape[:2]
    ch, cw = clean_image.shape[:2]
    crop = th if (tw == cw and th < ch) else None
    return float(np.abs(_thumb(clean_image, crop) - _thumb(template_image)).mean())


# Full-screen art the translation touched that keeps the 360 texture (by 360 index): backgrounds with
# a sign (the 360 redrew them in HD; 1987 t3 is even a different picture on the 360) and menu frames
# whose only change was removing an English logo. Full-screen pages with text (1980 help pages,
# 2041 "Parabens") are translated.
KEEP_360 = {
    1983: {2},          # capsule menu frame (logo removed on the PS2)
    1985: {17},         # Capsule Corp. background, sign
    1987: {3},          # PS2: Capsule Corp. background; 360: other picture
    2060: {1},          # menu frame
    2062: {16, 20},     # Kame House background, sign
}


def _keep_360(container, block, t_index):
    return container.file == "data_usi" and block < 0 and t_index in KEEP_360.get(container.entry, ())


def _plan_azt(container, block, tr_data, cl_data, azt_data, label):
    """Matches every 360 texture to its PS2 texture and plans the changed ones. Normally the index
    is the same; a few 360 containers were reordered, so the pictures are compared (clean PS2
    Spanish vs 360 Spanish) and a texture is remapped when another PS2 texture matches clearly
    better than the one at the same index."""
    ps2_tr, ps2_cl = ps2tex.parse(tr_data), ps2tex.parse(cl_data)
    azt = x360tex.parse(azt_data)
    valid = [j for j, (p, q) in enumerate(zip(ps2_tr, ps2_cl)) if 0 not in (p.width, p.height, q.width, q.height)]
    changed = {j for j in valid if _texture_changed(tr_data, ps2_tr[j], cl_data, ps2_cl[j])}
    container.blocks[block] = azt
    if not changed:
        return azt
    images = {}

    def clean(j):
        if j not in images:
            images[j] = ps2tex.decode(cl_data, ps2_cl[j])
        return images[j]

    def size(j):
        return ps2_cl[j].width, ps2_cl[j].height

    used = set()
    for t_index, texture in enumerate(azt.textures):
        if texture is None:
            continue
        t_size = (texture.width, texture.height)
        t_image = x360tex.decode(texture)[:texture.height, :texture.width]
        identity = None
        if t_index in valid and _compatible(size(t_index), t_size):
            identity = match_error(clean(t_index), t_image)
        best = None
        if identity is None or identity > 4.0:
            for j in valid:
                if j != t_index and _compatible(size(j), t_size):
                    e = match_error(clean(j), t_image)
                    if best is None or e < best[1]:
                        best = (j, e)
        if best and best[1] <= REMAP_MAX and (identity is None or best[1] < REMAP_RATIO * identity):
            mapped, error = best
            container.remapped.append((block, t_index, mapped, round(identity or -1, 2), round(error, 2)))
        elif identity is not None:
            mapped, error = t_index, identity
        else:
            if t_index in changed:
                container.unmatched.append((block, t_index))
            continue
        used.add(mapped)
        if mapped in changed and _keep_360(container, block, t_index):
            container.backgrounds.append((block, t_index, f"{ps2_cl[mapped].width}x{ps2_cl[mapped].height}"))
        elif mapped in changed:
            job = plan_texture(container.file, container.entry, block, t_index, tr_data,
                               ps2_tr[mapped], ps2_cl[mapped], texture)
            job.ps2_index, job.match = mapped, round(error, 2)
            container.jobs.append(job)
    for j in sorted(changed - used):
        container.missing.append((block, j, f"{ps2_tr[j].width}x{ps2_tr[j].height}"))
    return azt


def plan(translated, clean, spn, cmn_ps2_tr, cmn_ps2_cl, cmn, decompress, skip, log=print):
    """Containers to build. `skip` = data_usi entries already converted in step 1."""
    containers = []
    # 3a: #AMB menu containers
    for i in range(0, AMB_LAST + 1):
        tr_data, cl_data = translated.read(i), clean.read(i)
        if tr_data == cl_data:
            continue
        if tr_data[:4] != amb.AMB_MAGIC:
            raise amb.AmbError(f"entry {i}: expected #AMB")
        base = decompress(spn.read(i))
        c = Container("data_usi", i, "amb", base)
        ps2_tr, ps2_cl, x = amb.parse(tr_data, "<"), amb.parse(cl_data, "<"), amb.parse(base, ">")
        for k, (bt, bc) in enumerate(zip(ps2_tr.blocks, ps2_cl.blocks)):
            if bt == bc:
                continue
            if bt[:4] != ps2tex.AMT_MAGIC:
                raise amb.AmbError(f"entry {i} block {k}: the translation changed a non-texture block {bt[:4]!r}")
            _plan_azt(c, k, bt, bc, x.blocks[k], f"{i}.{k}")
        containers.append(c)
    # 3b/3c: top-level #AMT entries not converted in step 1 (resized, upscaled, 16-bit)
    for i in range(AMB_LAST + 1, afs_limit(translated, clean)):
        if i in skip:
            continue
        try:
            tr_data = translated.read(i)
        except afs.AfsError:
            continue
        if tr_data == clean.read(i) or tr_data[:4] != ps2tex.AMT_MAGIC:
            continue
        tr_data, cl_data = translated.read(i, sector_slack=True), clean.read(i, sector_slack=True)
        base = decompress(spn.read(i))
        if base[:4] != x360tex.AZT_MAGIC:
            log(f"[menus] entry {i}: 360 entry is not a texture, skipped")
            continue
        c = Container("data_usi", i, "azt", base)
        _plan_azt(c, -1, tr_data, cl_data, base, str(i))
        containers.append(c)
    # 3d: language-specific animations that go with the Spanish-layout textures
    for i in ACA_FROM_SPN:
        data = decompress(spn.read(i))
        if data[:4] != b"#ACA":
            raise amb.AmbError(f"entry {i}: expected #ACA in data_spn.afs")
        c = Container("data_usi", i, "aca", data)
        c.output = data
        containers.append(c)
    # 4: DATA_CMN signs
    for i in CMN_SIGNS:
        tr_data, cl_data = cmn_ps2_tr.read(i, sector_slack=True), cmn_ps2_cl.read(i, sector_slack=True)
        if tr_data == cl_data:
            continue
        base = decompress(cmn.read(i))
        c = Container("data_cmn", i, "azt", base)
        _plan_azt(c, -1, tr_data, cl_data, base, f"cmn {i}")
        containers.append(c)
    return containers


def afs_limit(translated, clean):
    return min(len(translated), len(clean), 2704)  # 2704: empty slot; 2705+ only exist on the 360


def render(job, upscaler):
    """Final RGBA image of the job at job.target."""
    from .upscale import lanczos, back_error
    if job.kind in ("same", "resized", "crop"):
        job.method = "direct"
        return job.source
    if upscaler is not None and job.keys is not None:
        image, job.method, job.error = upscaler.result(job.keys, job.source, job.target)
        return image
    image = lanczos(job.source, job.target)
    job.method, job.error = "lanczos", back_error(image, job.source)
    return image


def build_texture(job, image):
    w, h = job.target
    template = job.template
    if (template.width, template.height) == (w, h):
        texture = x360tex.Texture(template.words, template.dds)
    else:
        texture = x360tex.resized(template, w, h, job.ps2_log2)
    dw, dh = texture.dds_dimensions()
    canvas = np.zeros((dh, dw, 4), dtype=np.uint8)
    canvas[:h, :w] = image[:h, :w]
    new = x360tex.Texture(texture.words, x360tex.encode_like(texture, canvas))
    job.result = x360tex.decode(new)[:h, :w]
    return new


def build(containers, upscaler, log=print, fixer=None):
    """`fixer`: optional botoes.Fixer (PS2 button glyphs -> 360 glyphs, memory card messages): menu_pre()
    edits the PS2 source before upscaling, menu_post() the rendered image."""
    if fixer is not None:
        for c in containers:
            if c.kind == "aca":
                continue
            for job in c.jobs:
                fixer.menu_pre(c, job)
    if upscaler is not None:
        for c in containers:
            for job in c.jobs:
                if job.kind == "upscale" and job.target[0] > job.source.shape[1]:
                    job.keys = upscaler.add(job.source)
        upscaler.run()
    for c in containers:
        if c.kind == "aca":
            continue
        for job in c.jobs:
            image = render(job, upscaler)
            if fixer is not None:
                image = fixer.menu_post(c, job, image)
            c.blocks[job.block].textures[job.tex] = build_texture(job, image)
        if c.kind == "amb":
            x = amb.parse(c.base, ">")
            for k, azt in c.blocks.items():
                x.blocks[k] = x360tex.serialize(azt)
            c.output = amb.serialize(x)
        else:
            c.output = x360tex.serialize(c.blocks[-1])
    return containers


def validate(data, endian=">"):
    """Raises if `data` is not a well-formed 360 entry of a kind this tool writes."""
    magic = data[:4]
    if magic == x360tex.AZT_MAGIC:
        azt = x360tex.parse(data)
        for t in azt.textures:
            if t is not None:
                x360tex.decode(t)
        if x360tex.serialize(azt) != data:
            raise x360tex.AztError("#AZT does not round-trip")
    elif magic == amb.AMB_MAGIC:
        x = amb.parse(data, endian)
        for block in x.blocks:
            if block[:4] == x360tex.AZT_MAGIC:
                validate(block)
        if amb.serialize(x) != data:
            raise amb.AmbError("#AMB does not round-trip")
    elif magic == b"#ACA":
        if len(data) < 0x30:
            raise ValueError("#ACA too short")
    else:
        raise ValueError(f"unknown entry type {magic!r}")


# ---------------------------------------------------------------------------------------------------
# Contact sheets: PS2 PT-BR | generated 360 | original 360 English (data_usi.afs)
# ---------------------------------------------------------------------------------------------------

def english_texture(container, job, usi, cmn, decompress):
    archive = cmn if container.file == "data_cmn" else usi
    data = decompress(archive.read(container.entry))
    try:
        if job.block >= 0:
            data = amb.parse(data, ">").blocks[job.block]
        azt = x360tex.parse(data)
        texture = azt.textures[job.tex]
        return x360tex.decode(texture)[:texture.height, :texture.width]
    except (amb.AmbError, x360tex.AztError, IndexError, AttributeError, struct.error):
        return None


def pick_rows(containers, amb_samples=40):
    rows = []
    for c in containers:
        if c.kind == "azt" and c.file == "data_usi":
            special = [j for j in c.jobs if j.kind != "same"] or [j for j in c.jobs if j.template.fourcc == bytes(4)]
            rows += [(c, j) for j in special[:2]]
    amb_jobs = [(c, j) for c in containers if c.kind == "amb" for j in c.jobs]
    upscaled = [cj for cj in amb_jobs if cj[1].kind != "same"]
    others = [cj for cj in amb_jobs if cj[1].kind == "same"]
    take = upscaled[:amb_samples // 2]
    step = max(1, len(others) // max(1, amb_samples - len(take)))
    take += others[::step][:amb_samples - len(take)]
    rows += take
    rows += [(c, j) for c in containers if c.file == "data_cmn" for j in c.jobs]
    return rows


def contact_sheets(rows, usi, cmn, decompress, out_dir, per_sheet=12, max_width=560, prefix="menus",
                   titles=("PS2 PT-BR", "generated 360", "original 360 (English)")):
    """rows: (container, job) pairs, or (label, before, after, english) tuples of RGBA images."""
    from PIL import Image, ImageDraw

    def picture(image):
        if image is None:
            return Image.new("RGB", (120, 20), (60, 0, 0))
        alpha = image[..., 3:4].astype(np.float64) / 255.0
        rgb = (image[..., :3] * alpha + 96 * (1 - alpha)).astype(np.uint8)
        pic = Image.fromarray(rgb)
        if pic.width > max_width:
            pic = pic.resize((max_width, max(1, round(pic.height * max_width / pic.width))), Image.LANCZOS)
        if pic.height > max_width:
            pic = pic.resize((max(1, round(pic.width * max_width / pic.height)), max_width), Image.LANCZOS)
        return pic

    files = []
    for n in range(0, len(rows), per_sheet):
        chunk = []
        for row in rows[n:n + per_sheet]:
            if len(row) == 4:
                label, before, after, english = row
                chunk.append((label, picture(before), picture(after), picture(english)))
                continue
            c, job = row
            label = (f"{c.file} #{c.entry}" + (f" b{job.block}" if job.block >= 0 else "") +
                     f" t{job.tex} {job.kind} {job.method} "
                     f"{job.source.shape[1]}x{job.source.shape[0]}->{job.target[0]}x{job.target[1]}")
            chunk.append((label, picture(job.source), picture(job.result),
                          picture(english_texture(c, job, usi, cmn, decompress))))
        cols = [max(r[k].width for r in chunk) + 10 for k in (1, 2, 3)]
        heights = [max(r[1].height, r[2].height, r[3].height) + 16 for r in chunk]
        canvas = Image.new("RGB", (max(sum(cols), 600), 18 + sum(heights)), (40, 40, 40))
        draw = ImageDraw.Draw(canvas)
        for k, title in enumerate(titles):
            draw.text((sum(cols[:k]), 3), title, fill=(255, 255, 255))
        y = 18
        for (label, a, b, e), height in zip(chunk, heights):
            draw.text((0, y), label, fill=(255, 255, 0))
            for k, pic in enumerate((a, b, e)):
                canvas.paste(pic, (sum(cols[:k]), y + 13))
            y += height
        name = out_dir / f"{prefix}_{n // per_sheet + 1:02d}.png"
        canvas.save(name)
        files.append(name)
    return files
