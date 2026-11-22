#!/usr/bin/env python3
"""Builds the PT-BR (Brazilian Portuguese) translation pack for the Switch port from the user's own files.

Source: the fan translation of the PS2 "Budokai 3 Collector's Edition" (PT-BR v1.2), shipped as an
xdelta patch against the clean European CE ISO. The translation lives in USR/DATA_SPN.AFS (the
Spanish slot) and is image-based: every dialogue line and menu caption is a pre-rendered texture.
This tool converts those images into the Xbox 360 formats used by the port's game files and packs
the replacement entries into one file, ptbr.b3tr, that the port loads at run time.

Steps (one command runs them all; every step rewrites its own outputs, so re-runs are safe):
  1. patch     Translated ISO: reuse it if present (SHA-256 checked); otherwise extract the xdelta
               from the translation .7z with 7zr, apply it to the clean ISO with xdelta3, check.
  2. dialogue  Translated #AMT entries whose texture sizes equal the 360 ones (the dialogue lines):
               #AZT with DXT3/32-bit DDS, template data_usi.afs or data_spn.afs (same sizes).
               -> <work>/saida/entradas/NNNN.azt, index.json, report.txt, folhas/folha_NN.png
  3. menus     The 52 #AMB menu containers, the top-level menu textures the 360 redrew at about
               1.5x or the translators resized, 16-bit (A4R4G4B4) textures, and the three #ACA
               animations that go with the Spanish layout. Base: data_spn.afs (see ptbr/menus.py).
               Upscaling with Real-ESRGAN (realesrgan-ncnn-vulkan, realesr-animevideov3 x2, then
               Lanczos to the exact 360 size; Lanczos fallback when the quality guard trips).
  4. signs     DATA_CMN stage textures with translated signs (same size): data_cmn.afs.
               -> <work>/saida/menus/NNNN.{amb,azt,aca}, saida/cmn/NNNN.azt, menus.json,
                  report_menus.txt, folhas/menus_NN.png
  5. pack      B3TR v1 pack (ptbr/b3tr.py) -> <work>/saida/ptbr.b3tr and <pack-copy>.
     --verify-pack re-reads a pack, decompresses every entry, checks CRC32 and that each entry
     parses as a valid 360 entry, and compares the file table with the game files.

The game accepts raw (non-LZX) entries: it only runs XMemDecompress when an entry starts with the
LZX magic (sub_820A4700), so all replacement entries are stored uncompressed (inside the pack they
are zstd-compressed; the port decompresses them before handing them to the game).

Nothing under roms/ is modified; no translated data is part of this repository.

Usage (Windows, from the repository root):
  python tools/traducao_ptbr.py                    all steps, default paths of this machine's layout
  python tools/traducao_ptbr.py --only menus pack  some steps (they reuse earlier outputs)
  python tools/traducao_ptbr.py --verify-pack      check <work>/saida/ptbr.b3tr
  python tools/traducao_ptbr.py --help
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib
import lzma
from pathlib import Path

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # also works under python -I
from ptbr import afs, amb, b3tr, botoes, hdmsg, menus, ps2tex, upscale, x360tex, xmem  # noqa: E402

PATCHED_ISO_SHA256 = "d801e50865b0df46b4d66b0da184121a421156616cbb02ce9e5c382e9b87b2bc"
XDELTA_NAME = "DBZB3-CE-PTBR-V1.2.xdelta"
PATCHED_ISO_NAME = "DBZB3-CE-PTBR-V1.2.iso"
PS2_ENTRY_LIMIT = 2705  # PS2 and 360 archives match index by index up to here; 360 adds 2705..2708

REPO = Path(__file__).resolve().parents[1]
PROJECT = REPO.parents[1]          # ...\Meuprojeto360\Dragon Ball Z Budokai HD Collection
GAMES = PROJECT.parents[1]         # ...\NintendoSwitch\Jogos
PS2_DIR = GAMES / "roms" / "psx2" / "Dragon Ball Z - Budokai 3"
USOGERAL = GAMES / "usogeral"


def log(message):
    print(message, flush=True)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 24), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------------------------------------------
# Step 1: translated ISO
# ---------------------------------------------------------------------------------------------------

def ensure_patched_iso(args):
    target = Path(args.patched_iso)
    if target.exists():
        log(f"[iso] checking {target.name} ...")
        digest = sha256_of(target)
        if digest != PATCHED_ISO_SHA256:
            sys.exit(f"[iso] {target} has SHA-256 {digest}, expected {PATCHED_ISO_SHA256}. Delete it to rebuild.")
        log("[iso] SHA-256 ok")
        return target
    for tool in (args.sevenzip, args.xdelta3, args.clean_iso, args.translation_7z):
        if not Path(tool).exists():
            sys.exit(f"[iso] missing: {tool}")
    temp = target.parent / "_xdelta_tmp"
    temp.mkdir(parents=True, exist_ok=True)
    try:
        log(f"[iso] extracting {XDELTA_NAME} from the translation archive (about 1.7 GB) ...")
        subprocess.run([args.sevenzip, "e", "-y", f"-o{temp}", args.translation_7z, XDELTA_NAME],
                       check=True, stdout=subprocess.DEVNULL)
        partial = target.with_suffix(".iso.part")
        log("[iso] applying the xdelta to the clean CE ISO (a few minutes) ...")
        subprocess.run([args.xdelta3, "-d", "-f", "-s", args.clean_iso, str(temp / XDELTA_NAME), str(partial)],
                       check=True)
        digest = sha256_of(partial)
        if digest != PATCHED_ISO_SHA256:
            partial.unlink()
            sys.exit(f"[iso] patched ISO has SHA-256 {digest}, expected {PATCHED_ISO_SHA256}. "
                     "Is the clean ISO the European Collector's Edition (SLES_533.46)?")
        partial.replace(target)
        log("[iso] SHA-256 ok")
    finally:
        shutil.rmtree(temp, ignore_errors=True)  # the xdelta can always be extracted again
    return target


# ---------------------------------------------------------------------------------------------------
# Steps 2-3: conversion
# ---------------------------------------------------------------------------------------------------

def premultiplied(image):
    image = image.astype(np.float64)
    return np.concatenate([image[..., :3] * image[..., 3:] / 255.0, image[..., 3:]], axis=-1)


def image_error(source, result):
    """(max, mean) absolute error on premultiplied RGBA over the texture's own w x h area."""
    diff = np.abs(premultiplied(source) - premultiplied(result))
    return float(diff.max()), float(diff.mean())


def ps2_dims(textures):
    return [(t.width, t.height) for t in textures]


def convert_entry(ps2_data, ps2_textures, template, edit=None):
    """New #AZT bytes plus per-texture errors, or raises x360tex.AztError / ps2tex.AmtError.
    `edit(index, image, all_images)`: optional in-place change of each decoded picture before encoding."""
    textures = []
    errors = []
    sources = []
    for ps2, tex in zip(ps2_textures, template.textures):
        width, height = tex.dds_dimensions()
        if ps2.width > width or ps2.height > height:
            raise x360tex.AztError("PS2 image larger than the 360 texture")
        sources.append(ps2tex.decode(ps2_data, ps2, (width, height)))
    if edit is not None:
        for k, source in enumerate(sources):
            edit(k, source, sources)
    for ps2, tex, source in zip(ps2_textures, template.textures, sources):
        dds = x360tex.encode_like(tex, source)
        new = x360tex.Texture(tex.words, dds)
        result = x360tex.decode(new)
        errors.append(image_error(source[:ps2.height, :ps2.width], result[:ps2.height, :ps2.width]))
        textures.append(new)
    return x360tex.serialize(x360tex.Azt(template.header, textures)), errors


def baseline_error(clean_data, template):
    """Error of the game's own conversion: clean PS2 Spanish entry vs the 360 data_spn texture it became.
    Same metric as ours, so the two can be compared. None when the sizes differ."""
    try:
        clean_textures = ps2tex.parse(clean_data)
        if ps2_dims(clean_textures) != template.dims():
            return None
        errors = []
        for ps2, tex in zip(clean_textures, template.textures):
            if tex.fourcc != b"DXT3":
                return None
            width, height = tex.dds_dimensions()
            source = ps2tex.decode(clean_data, ps2, (width, height))[:ps2.height, :ps2.width]
            errors.append(image_error(source, x360tex.decode(tex)[:ps2.height, :ps2.width]))
        return [max(e[0] for e in errors), round(sum(e[1] for e in errors) / len(errors), 4)]
    except (ps2tex.AmtError, x360tex.AztError, struct.error):
        return None


def convert(args, patched_iso, fixer=None):
    work = Path(args.work)
    out_dir = work / "saida" / "entradas"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    xmemd = Path(args.xmemd)
    if not xmemd.exists():
        log(f"[lzx] building {xmemd} ...")
        xmem.build(str(xmemd), args.clang, args.libmspack)
    decompress = xmem.Decompressor(str(xmemd))

    game_us = Path(args.game_root) / "us"
    translated = afs.open_in_iso(str(patched_iso), "USR/DATA_SPN.AFS")
    clean = afs.open_in_iso(args.clean_iso, "USR/DATA_SPN.AFS")
    usi = afs.open_file(str(game_us / "data_usi.afs"))
    spn = afs.open_file(str(game_us / "data_spn.afs"))
    log(f"[afs] PS2 DATA_SPN {len(clean)} entries (translated {len(translated)}), "
        f"360 data_usi {len(usi)}, data_spn {len(spn)}")

    index, deferred = [], {}
    changed = 0
    started = time.time()
    for i in range(min(len(translated), len(clean), PS2_ENTRY_LIMIT)):
        def defer(reason):
            deferred.setdefault(reason, []).append(i)

        try:
            data = translated.read(i)
        except afs.AfsError:
            # 2704 is an empty slot in the clean archive; the translated one has garbage there
            defer("translated AFS slot points outside the archive (empty slot, ignored)")
            continue
        if data == clean.read(i):
            continue
        changed += 1

        if data[:4] != ps2tex.AMT_MAGIC:
            defer(f"PS2 type {data[:4].decode('latin-1')} (menus step)")
            continue
        data = translated.read(i, sector_slack=True)  # a few translated entries are a little short
        try:
            ps2_textures = ps2tex.parse(data)
        except (ps2tex.AmtError, struct.error) as error:
            defer(f"PS2 #AMT unreadable: {error}")
            continue
        template, template_name = None, None
        for archive, name in ((usi, "data_usi"), (spn, "data_spn")):
            candidate = decompress(archive.read(i))
            if candidate[:4] != x360tex.AZT_MAGIC:
                continue
            parsed = x360tex.parse(candidate)
            if parsed.dims() == ps2_dims(ps2_textures):
                template, template_name = parsed, name
                break
        if template is None:
            usi_data = decompress(usi.read(i))
            if usi_data[:4] != x360tex.AZT_MAGIC:
                defer("360 entry is not a texture (dummy/unused on 360)")
            else:
                defer("texture sizes differ from the 360 entry (upscaled UI or resized by the translators; menus step)")
            continue
        try:
            edit = None
            if fixer is not None:
                edit = lambda k, image, images, i=i: fixer.dialogue(i, k, image, images)  # noqa: E731
            output, errors = convert_entry(data, ps2_textures, template, edit)
        except (x360tex.AztError, ps2tex.AmtError) as error:
            defer(f"conversion failed: {error}")
            continue
        (out_dir / f"{i:04d}.azt").write_bytes(output)
        reference = baseline_error(clean.read(i, sector_slack=True), template) if template_name == "data_spn" else None
        index.append({
            "entry": i,
            "size": len(output),
            "sha256": hashlib.sha256(output).hexdigest(),
            "source": f"{PATCHED_ISO_NAME}::USR/DATA_SPN.AFS#{i}",
            "template": f"{template_name}.afs#{i}",
            "textures": len(errors),
            "formats": sorted({t.fourcc.decode("latin-1").strip("\0") or "RGBA32" for t in template.textures}),
            "max_error": max(e[0] for e in errors),
            "mean_error": round(sum(e[1] for e in errors) / len(errors), 4),
            "reference_error": reference,
        })
    decompress.close()
    elapsed = time.time() - started
    log(f"[convert] {changed} changed entries, {len(index)} converted in {elapsed:.1f} s")
    return index, deferred, changed, elapsed



# ---------------------------------------------------------------------------------------------------
# Step 4: checks, sizes, contact sheets, report
# ---------------------------------------------------------------------------------------------------

def roundtrip_check(args):
    """Parsing then serializing every #AZT of data_usi.afs must give the decompressed bytes back."""
    decompress = xmem.Decompressor(args.xmemd)
    usi = afs.open_file(str(Path(args.game_root) / "us" / "data_usi.afs"))
    same = total = 0
    mismatches = []
    for i in range(len(usi)):
        data = decompress(usi.read(i))
        if data[:4] != x360tex.AZT_MAGIC:
            continue
        total += 1
        if x360tex.serialize(x360tex.parse(data)) == data:
            same += 1
        else:
            mismatches.append(i)
    decompress.close()
    return same, total, mismatches


def measure_sizes(out_dir, index):
    raw = b"".join((out_dir / f"{e['entry']:04d}.azt").read_bytes() for e in index)
    per_entry_deflate = sum(len(zlib.compress((out_dir / f"{e['entry']:04d}.azt").read_bytes(), 9)) for e in index)
    return {
        "raw": len(raw),
        "deflate9_per_entry": per_entry_deflate,
        "deflate9_whole": len(zlib.compress(raw, 9)),
        "lzma_whole": len(lzma.compress(raw, preset=6)),
    }


def contact_sheets(args, patched_iso, index, count=40, per_sheet=10):
    sys.path.append(args.pillow)
    from PIL import Image, ImageDraw

    sheets_dir = Path(args.work) / "folhas"
    if sheets_dir.exists():
        shutil.rmtree(sheets_dir)
    sheets_dir.mkdir(parents=True)
    decompress = xmem.Decompressor(args.xmemd)
    translated = afs.open_in_iso(str(patched_iso), "USR/DATA_SPN.AFS")
    usi = afs.open_file(str(Path(args.game_root) / "us" / "data_usi.afs"))
    out_dir = Path(args.work) / "saida" / "entradas"
    step = max(1, len(index) // count)
    picks = [e["entry"] for e in index[::step]][:count]

    def on_grey(image):
        alpha = image[..., 3:4].astype(np.float64) / 255.0
        return (image[..., :3] * alpha + 96 * (1 - alpha)).astype(np.uint8)

    rows = []
    for i in picks:
        data = translated.read(i)
        ps2 = ps2tex.parse(data)[0]
        mine = x360tex.parse((out_dir / f"{i:04d}.azt").read_bytes()).textures[0]
        original = x360tex.parse(decompress(usi.read(i))).textures[0]
        w, h = ps2.width, ps2.height
        a = ps2tex.decode(data, ps2)
        b = x360tex.decode(mine)[:h, :w]
        ow, oh = original.width, original.height
        c = x360tex.decode(original)[:oh, :ow]
        rows.append((i, on_grey(a), on_grey(b), on_grey(c)))
    decompress.close()

    files = []
    for sheet in range(0, len(rows), per_sheet):
        chunk = rows[sheet:sheet + per_sheet]
        col = max(max(r[1].shape[1], r[3].shape[1]) for r in chunk) + 8
        heights = [max(r[1].shape[0], r[3].shape[0]) + 8 for r in chunk]
        label = 48
        canvas = Image.new("RGB", (label + 3 * col, 20 + sum(heights)), (40, 40, 40))
        draw = ImageDraw.Draw(canvas)
        draw.text((label, 4), "PS2 PT-BR", fill=(255, 255, 255))
        draw.text((label + col, 4), "generated 360 (DXT3)", fill=(255, 255, 255))
        draw.text((label + 2 * col, 4), "original 360 (English)", fill=(255, 255, 255))
        y = 20
        for (i, a, b, c), height in zip(chunk, heights):
            draw.text((4, y + 4), f"#{i}", fill=(255, 255, 0))
            for k, image in enumerate((a, b, c)):
                canvas.paste(Image.fromarray(image), (label + k * col, y))
            y += height
        name = sheets_dir / f"folha_{sheet // per_sheet + 1:02d}.png"
        canvas.save(name)
        files.append(name)
    return files, picks


def write_report(args, index, deferred, changed, elapsed, roundtrip, sizes, sheets, timings):
    work = Path(args.work) / "saida"
    (work / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    templates = {}
    for e in index:
        templates[e["template"].split(".")[0]] = templates.get(e["template"].split(".")[0], 0) + 1
    worst = sorted(index, key=lambda e: -e["max_error"])[:5]
    refs = [e["reference_error"] for e in index if e["reference_error"]]
    lines = [
        "PT-BR dialogue conversion (step 1) - report",
        "",
        f"changed entries in PS2 DATA_SPN (translated vs clean): {changed}",
        f"converted dialogue entries: {len(index)} (templates: {templates})",
        f"conversion time: {elapsed:.1f} s",
        "",
        "deferred (not converted in this step):",
    ]
    for reason, items in sorted(deferred.items(), key=lambda kv: -len(kv[1])):
        lines.append(f"  {len(items):5d}  {reason}  e.g. {items[:8]}")
    same, total, mismatches = roundtrip
    lines += [
        "",
        f"writer round trip on data_usi.afs #AZT entries: {same}/{total} byte-identical"
        + (f", mismatches {mismatches[:10]}" if mismatches else ""),
        "image error vs PS2 source (premultiplied RGBA, 0-255, DXT3 + 4-bit alpha):",
        f"  max over all entries {max(e['max_error'] for e in index):.1f}, "
        f"mean of entry means {sum(e['mean_error'] for e in index) / len(index):.4f}",
        "  worst entries: " + ", ".join(f"#{e['entry']} max {e['max_error']:.0f}" for e in worst),
        "  reference, the game's own PS2->360 conversion of the same Spanish entries (data_spn), same metric:",
        f"    {len(refs)} entries: max {max(r[0] for r in refs):.1f}, mean of entry means "
        f"{sum(r[1] for r in refs) / len(refs):.4f}; ours on those entries: max "
        f"{max(e['max_error'] for e in index if e['reference_error']):.1f}, mean "
        f"{sum(e['mean_error'] for e in index if e['reference_error']) / len(refs):.4f}",
        "",
        "sizes of the converted entries:",
        f"  uncompressed {sizes['raw']:,} B",
        f"  deflate -9 per entry {sizes['deflate9_per_entry']:,} B, whole {sizes['deflate9_whole']:,} B",
        f"  lzma whole {sizes['lzma_whole']:,} B",
        "",
        "contact sheets: " + ", ".join(str(s) for s in sheets),
        "",
        "timings: " + ", ".join(f"{k} {v:.1f} s" for k, v in timings.items()),
    ]
    (work / "report.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    log("\n".join(lines))


# ---------------------------------------------------------------------------------------------------
# Steps 3-4: menus, #AMB, #ACA, DATA_CMN signs
# ---------------------------------------------------------------------------------------------------

def menus_step(args, patched_iso, fixer=None):
    started = time.time()
    work = Path(args.work)
    out_usi, out_cmn, out_hd = work / "saida" / "menus", work / "saida" / "cmn", work / "saida" / "hd"
    for d in (out_usi, out_cmn, out_hd):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
    index_path = work / "saida" / "index.json"
    if not index_path.exists():
        sys.exit("[menus] run the dialogue step first (saida/index.json missing)")
    skip = {e["entry"] for e in json.loads(index_path.read_text(encoding="utf-8"))}

    xmemd = Path(args.xmemd)
    if not xmemd.exists():
        xmem.build(str(xmemd), args.clang, args.libmspack)
    decompress = xmem.Decompressor(str(xmemd))
    game_us = Path(args.game_root) / "us"
    translated = afs.open_in_iso(str(patched_iso), "USR/DATA_SPN.AFS")
    clean = afs.open_in_iso(args.clean_iso, "USR/DATA_SPN.AFS")
    cmn_tr = afs.open_in_iso(str(patched_iso), "USR/DATA_CMN.AFS")
    cmn_cl = afs.open_in_iso(args.clean_iso, "USR/DATA_CMN.AFS")
    usi = afs.open_file(str(game_us / "data_usi.afs"))
    spn = afs.open_file(str(game_us / "data_spn.afs"))
    cmn = afs.open_file(str(game_us / "data_cmn.afs"))

    log("[menus] matching translated textures with the 360 containers (about a minute) ...")
    containers = menus.plan(translated, clean, spn, cmn_tr, cmn_cl, cmn, decompress, skip, log)
    t_plan = time.time() - started
    upscaler = None
    if not args.no_upscale:
        upscaler = upscale.Upscaler(args.realesrgan, work / "cache" / "realesrgan", args.upscale_model,
                                    args.upscale_scale, log)
        if not upscaler.available():
            log(f"[menus] {args.realesrgan} not found: using Lanczos only (--realesrgan to set it)")
            upscaler = None
    menus.build(containers, upscaler, log, fixer)

    records = []
    for c in containers:
        menus.validate(c.output)
        target = (out_cmn if c.file == "data_cmn" else out_usi) / f"{c.entry:04d}.{c.kind}"
        target.write_bytes(c.output)
        records.append({
            "file": c.file + ".afs", "entry": c.entry, "kind": c.kind, "size": len(c.output),
            "sha256": hashlib.sha256(c.output).hexdigest(),
            "base": ("data_cmn" if c.file == "data_cmn" else "data_spn") + f".afs#{c.entry}",
            "textures": [{"block": j.block, "tex": j.tex, "ps2": j.ps2_index, "kind": j.kind,
                          "method": j.method, "size": list(j.target), "match": j.match,
                          "back_error": round(j.error, 3)} for j in c.jobs],
            "missing": c.missing, "backgrounds": c.backgrounds, "remapped": c.remapped, "unmatched": c.unmatched,
        })
    (work / "saida" / "menus.json").write_text(json.dumps(records, indent=1), encoding="utf-8")

    # HD-only message of the Story mode exit (no PS2 counterpart): composed from the 360 font
    hd_data, hd_done = hdmsg.build(usi, decompress)
    menus.validate(hd_data)
    (out_hd / f"{hdmsg.ENTRY:04d}.azt").write_bytes(hd_data)
    log(f"[menus] HD-only message data_usi #{hdmsg.ENTRY} textures {[k for k, _, _ in hd_done]} -> PT-BR")
    if fixer is not None:
        for k, original, picture in hd_done:
            fixer.sheet_rows.append((f"data_usi #{hdmsg.ENTRY} t{k} HD-only message (composed from the 360 font)",
                                     original, picture, (hdmsg.ENTRY, -1, k)))
            fixer.records.append({"file": "data_usi", "entry": hdmsg.ENTRY, "block": -1, "tex": k, "ps2": -1,
                                  "kind": "hd-only", "glyphs": [], "memory": [],
                                  "text": hdmsg.MESSAGES[k]})

    sheets = []
    if args.sheets:
        sheets_dir = work / "folhas"
        sheets_dir.mkdir(parents=True, exist_ok=True)
        for old in sheets_dir.glob("menus_*.png"):
            old.unlink()
        sheets = menus.contact_sheets(menus.pick_rows(containers), usi, cmn, decompress, sheets_dir)
    decompress.close()
    write_menus_report(work, containers, sheets, upscaler, t_plan, time.time() - started)


def texture_format(texture):
    flags, fourcc, bits = struct.unpack_from("<I4sI", texture.dds, 0x50)
    return fourcc.decode("latin-1").strip("\0") or f"RGB{bits}"


def write_menus_report(work, containers, sheets, upscaler, t_plan, elapsed):
    from collections import Counter
    jobs = [(c, j) for c in containers for j in c.jobs]
    kinds = Counter((c.file, c.kind, j.kind) for c, j in jobs)
    methods = Counter(j.method for c, j in jobs)
    formats = Counter(texture_format(j.template) for c, j in jobs)
    fallbacks = [(c.entry, j.block, j.tex, round(j.error, 2)) for c, j in jobs if "fallback" in j.method]
    differs = sorted({c.entry for c, j in jobs if j.match > menus.DIFFERS})
    top = [c for c in containers if c.kind == "azt" and c.file == "data_usi"]
    lines = [
        "PT-BR menus, #AMB, #ACA and DATA_CMN signs (steps 3-4) - report", "",
        f"containers: {dict(Counter((c.file, c.kind) for c in containers))}",
        f"  #AMB menu containers rebuilt (base data_spn.afs): {sum(1 for c in containers if c.kind == 'amb')}",
        f"  top-level menu #AZT entries (base data_spn.afs): {len(top)} "
        f"({sum(1 for c in top if any(j.kind != 'same' for j in c.jobs))} with resized/upscaled/cropped textures)",
        f"  #ACA taken from data_spn.afs: {[c.entry for c in containers if c.kind == 'aca']}",
        f"  data_cmn.afs sign textures: {[c.entry for c in containers if c.file == 'data_cmn']}",
        f"textures replaced: {len(jobs)}",
    ]
    for key, n in sorted(kinds.items()):
        lines.append(f"  {n:5d}  {key[0]} {key[1]} {key[2]}")
    lines.append(f"360 formats of the replaced textures: {dict(formats)}")
    lines.append(f"methods: {dict(methods)}")
    if upscaler:
        lines.append(f"upscaler: {upscaler.exe.name} model {upscaler.model} x{upscaler.scale}, then Lanczos to "
                     f"the 360 size; Lanczos fallback when the back error is above {upscale.FALLBACK_MIN_ERROR} and "
                     f"{upscale.FALLBACK_RATIO}x the Lanczos one")
        if upscaler.stats:
            ours = sorted(e for e, r in upscaler.stats)
            ref = sorted(r for e, r in upscaler.stats)
            lines.append(f"  back error (mean premultiplied, 0-255) median/max: Real-ESRGAN {ours[len(ours) // 2]:.2f}/"
                         f"{ours[-1]:.2f}, Lanczos {ref[len(ref) // 2]:.2f}/{ref[-1]:.2f}")
    lines.append(f"Lanczos fallbacks ({len(fallbacks)}) (entry, block, texture, back error): {fallbacks}")
    lines += ["", "PS2 textures the translation changed that have no 360 counterpart (skipped):"]
    lines += [f"  #{c.entry}: {c.missing}" for c in containers if c.missing]
    lines.append("translated full-screen backgrounds/frames kept as on the 360 (HD art, menus.KEEP_360; block, 360 index, PS2 size):")
    lines += [f"  #{c.entry}: {c.backgrounds}" for c in containers if c.backgrounds]
    lines.append("360 textures reordered against the PS2 container, remapped by picture "
                 "(block, 360 index, PS2 index, error at the same index or -1, error):")
    lines += [f"  #{c.entry}: {c.remapped}" for c in containers if c.remapped]
    lines.append("360 textures left as in data_spn.afs (the 360 changed them and no PS2 picture matches):")
    lines += [f"  #{c.entry}: {c.unmatched}" for c in containers if c.unmatched]
    lines.append(f"entries where the 360 reworded some texts (the translation of the PS2 wording is used): {differs}")
    lines += ["", "contact sheets: " + ", ".join(str(s) for s in sheets),
              f"timings: plan {t_plan:.1f} s, total {elapsed:.1f} s"]
    (work / "saida" / "report_menus.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    log("\n".join(lines))


# ---------------------------------------------------------------------------------------------------
# Step 5: pack
# ---------------------------------------------------------------------------------------------------

PACK_FILES = ("data_usi.afs", "data_cmn.afs")


NEVER_PACK = {4}   # data_usi / data_cmn entry 4: fixed-buffer load in the game


def collect_entries(work):
    saida = Path(work) / "saida"
    entries = {}
    for file_id, folder in ((0, saida / "entradas"), (0, saida / "menus"), (0, saida / "hd"), (1, saida / "cmn")):
        if not folder.exists():
            continue
        for item in sorted(folder.iterdir()):
            index = int(item.stem)
            if index in NEVER_PACK:
                # loaded into a fixed-size buffer by the game: a changed size breaks it
                log(f"[pack] {PACK_FILES[file_id]} #{index} is never packed: skipped {item}")
                continue
            if (file_id, index) in entries:
                sys.exit(f"[pack] entry {index} of {PACK_FILES[file_id]} produced twice ({item})")
            entries[(file_id, index)] = item.read_bytes()
    return entries


def pack_step(args):
    started = time.time()
    entries = collect_entries(args.work)
    if not entries:
        sys.exit("[pack] nothing to pack: run the dialogue/menus steps first")
    game_us = Path(args.game_root) / "us"
    files = [(name, str(game_us / name)) for name in PACK_FILES]
    target = Path(args.pack)
    log(f"[pack] {len(entries)} entries -> {target} (zstd level {args.zstd_level}) ...")
    stats = b3tr.write(target, files, entries, args.zstd_level, log)
    counts = {name: sum(1 for (f, _) in entries if f == k) for k, name in enumerate(PACK_FILES)}
    copies = []
    if args.pack_copy:
        copy = Path(args.pack_copy)
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(target, copy)
        copies.append(str(copy))
    lines = [
        "PT-BR pack (step 5) - report", "",
        f"pack: {target} ({stats['size']:,} B, codec {'zstd' if stats['codec'] else 'raw'})",
        f"copies: {copies}",
        f"entries: {sum(counts.values())} {counts}",
        f"payload: {stats['raw']:,} B uncompressed, {stats['stored']:,} B stored",
        f"time: {time.time() - started:.1f} s",
    ]
    (Path(args.work) / "saida" / "report_pack.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    log("\n".join(lines))


def verify_pack(path, game_root):
    started = time.time()
    files, entries, fh = b3tr.read(path)
    problems = []
    game_us = Path(game_root) / "us"
    for f in files:
        local = game_us / f["name"]
        if not local.exists():
            problems.append(f"{f['name']}: not found in {game_us}")
            continue
        size, count, crc = b3tr.file_info(local)
        if (size, count, crc) != (f["original_size"], f["afs_entry_count"], f["original_crc32"]):
            problems.append(f"{f['name']}: built for size {f['original_size']} crc {f['original_crc32']:08x}, "
                            f"local file has size {size} crc {crc:08x}")
    previous = None
    raw = 0
    for e in entries:
        key = (e["file_id"], e["entry_index"])
        if previous is not None and key <= previous:
            problems.append(f"entry table not sorted at {key}")
        previous = key
        if e["data_offset"] % 16:
            problems.append(f"entry {key} not 16-byte aligned")
        if e["file_id"] >= len(files) or e["entry_index"] >= files[e["file_id"]]["afs_entry_count"]:
            problems.append(f"entry {key} out of range")
            continue
        try:
            data = b3tr.load(fh, e)
            menus.validate(data)
            raw += len(data)
        except Exception as error:  # report every bad entry, not only the first one
            problems.append(f"entry {key}: {error}")
    fh.close()
    per_file = {f["name"]: sum(1 for e in entries if e["file_id"] == k) for k, f in enumerate(files)}
    log(f"[verify] {path}: {len(entries)} entries {per_file}, {raw:,} B uncompressed, "
        f"{os.path.getsize(path):,} B on disk, {time.time() - started:.1f} s")
    for problem in problems[:50]:
        log(f"[verify] PROBLEM {problem}")
    log("[verify] OK" if not problems else f"[verify] {len(problems)} problem(s)")
    return not problems


# ---------------------------------------------------------------------------------------------------

def make_fixer(args):
    """botoes.Fixer (PS2 glyphs -> 360 glyphs, memory card messages) or None with --keep-ps2-buttons."""
    if args.keep_ps2_buttons:
        return None
    xmemd = Path(args.xmemd)
    if not xmemd.exists():
        xmem.build(str(xmemd), args.clang, args.libmspack)
    decompress = xmem.Decompressor(str(xmemd))
    usi = afs.open_file(str(Path(args.game_root) / "us" / "data_usi.afs"))
    clean = afs.open_in_iso(args.clean_iso, "USR/DATA_SPN.AFS")
    translated = afs.open_in_iso(args.patched_iso, "USR/DATA_SPN.AFS")
    fixer = botoes.Fixer(usi, decompress, clean, translated)
    fixer.usi, fixer.decompress = usi, decompress
    return fixer


def buttons_report(args, fixer, steps):
    """saida/botoes.json, saida/report_botoes.txt and folhas/botoes_NN.png."""
    from types import SimpleNamespace
    from ptbr import memoria
    work = Path(args.work)
    glyphs, memory = fixer.summary()
    (work / "saida" / "botoes.json").write_text(json.dumps(
        {"steps": sorted(steps), "textures": fixer.records, "review": fixer.review}, indent=1), encoding="utf-8")
    sheets = []
    if args.sheets and fixer.sheet_rows:
        sheets_dir = work / "folhas"
        sheets_dir.mkdir(parents=True, exist_ok=True)
        for old in sheets_dir.glob("botoes_*.png"):
            old.unlink()

        def english(ref):
            entry, block, tex = ref
            return menus.english_texture(SimpleNamespace(file="data_usi", entry=entry),
                                         SimpleNamespace(block=block, tex=tex), fixer.usi, None, fixer.decompress)

        # HD-only message and memory card pictures first, then the glyph pictures
        first = set(memoria.ENTRIES) | {hdmsg.ENTRY}
        rows = sorted(fixer.sheet_rows, key=lambda r: (r[3][0] not in first, r[3]))
        rows = [(label, before, after, english(ref)) for label, before, after, ref in rows]
        sheets = menus.contact_sheets(rows, fixer.usi, None, fixer.decompress, sheets_dir, per_sheet=14,
                                      prefix="botoes", titles=("antes (PS2 PT-BR)", "depois",
                                                               "360 original (ingles)"))
    lines = ["PS2 buttons -> 360 buttons, memory card messages - report", "",
             f"edited textures: {len(fixer.records)}",
             "glyphs replaced (kind PS2->360: count): " + ", ".join(
                 f"{k[0]} {k[1]}->{k[2]}: {n}" for k, n in sorted(glyphs.items())),
             "memory card edits: " + ", ".join(f"{k}: {n}" for k, n in sorted(memory.items())),
             f"for review ({len(fixer.review)}):"] + ["  " + r for r in fixer.review] + \
            ["", "contact sheets: " + ", ".join(str(s) for s in sheets)]
    (work / "saida" / "report_botoes.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    log("\n".join(lines[:5]) + f"\n[buttons] {len(sheets)} contact sheets, {len(fixer.review)} items for review")


STEPS = ("patch", "dialogue", "menus", "pack")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        epilog="Steps: patch (translated ISO), dialogue (step 1), menus (steps 3-4: menus, #AMB, #ACA, "
               "DATA_CMN signs), pack (B3TR v1). Copy the pack to the SD card as switch/B3deko/traducao/ptbr.b3tr.")
    parser.add_argument("--clean-iso", default=str(PS2_DIR / "DragonBall Z - Budokai 3 - Collector's Edition (Europe) (En,Ja,Fr,De,Es,It).iso"))
    parser.add_argument("--translation-7z", default=str(PS2_DIR / "tradução" / "DBZ Budokai 3 - Collectors Edition (PT-BR) (V1.2).7z"))
    parser.add_argument("--game-root", default=str(PROJECT / "B3deko" / "game_root"))
    parser.add_argument("--work", default=str(PROJECT / "trabalho" / "traducao-ptbr"))
    parser.add_argument("--patched-iso", default=None, help="default: <work>/ce/" + PATCHED_ISO_NAME)
    parser.add_argument("--sevenzip", default=str(USOGERAL / "ferramentas" / "7zip-26.06" / "7zr.exe"))
    parser.add_argument("--xdelta3", default=str(USOGERAL / "ferramentas" / "xdelta3-3.1.0" / "xdelta3-3.1.0-x86_64.exe"))
    parser.add_argument("--xmemd", default=None, help="default: <work>/bin/xmemd.exe (built when missing)")
    parser.add_argument("--clang", default=str(USOGERAL / "compilacao" / "llvm" / "llvm-mingw-20261020-ucrt-x86_64" / "bin" / "clang.exe"))
    parser.add_argument("--libmspack", default=str(USOGERAL / "bibliotecas" / "rexglue-0.10" / "libmspack" / "libmspack"))
    parser.add_argument("--pillow", default=str(USOGERAL / "bibliotecas" / "python-pillow"))
    parser.add_argument("--realesrgan", default=str(USOGERAL / "ferramentas" / "realesrgan-ncnn-vulkan-20220424" / "realesrgan-ncnn-vulkan.exe"))
    parser.add_argument("--upscale-model", default=upscale.DEFAULT_MODEL)
    parser.add_argument("--upscale-scale", type=int, default=upscale.DEFAULT_SCALE)
    parser.add_argument("--no-upscale", action="store_true", help="Lanczos only (no Real-ESRGAN)")
    parser.add_argument("--zstd-level", type=int, default=19)
    parser.add_argument("--pack", default=None, help="default: <work>/saida/ptbr.b3tr")
    parser.add_argument("--pack-copy", default=str(PROJECT / "B3deko" / "traducao" / "ptbr.b3tr"),
                        help="second copy of the pack ('' = none)")
    parser.add_argument("--sheets", type=int, default=40, help="dialogue lines in the contact sheets (0 = no sheets)")
    parser.add_argument("--only", nargs="+", choices=STEPS, help="run only these steps (in pipeline order)")
    parser.add_argument("--keep-ps2-buttons", action="store_true",
                        help="keep the PS2 button glyphs and the PS2 memory card wording of the translation")
    parser.add_argument("--verify-pack", nargs="?", const="", default=None, metavar="PACK",
                        help="check a pack (default <work>/saida/ptbr.b3tr) and exit")
    args = parser.parse_args()
    sys.path.append(args.pillow)
    args.patched_iso = args.patched_iso or str(Path(args.work) / "ce" / PATCHED_ISO_NAME)
    args.xmemd = args.xmemd or str(Path(args.work) / "bin" / "xmemd.exe")
    args.pack = args.pack or str(Path(args.work) / "saida" / "ptbr.b3tr")

    if args.verify_pack is not None:
        sys.exit(0 if verify_pack(args.verify_pack or args.pack, args.game_root) else 1)

    steps = set(args.only or STEPS)
    timings = {}
    t = time.time()
    patched = ensure_patched_iso(args) if steps & {"patch", "dialogue", "menus"} else None
    timings["iso"] = time.time() - t
    fixer = make_fixer(args) if steps & {"dialogue", "menus"} else None
    if "dialogue" in steps:
        index, deferred, changed, elapsed = convert(args, patched, fixer)
        timings["convert"] = elapsed
        t = time.time()
        roundtrip = roundtrip_check(args)
        timings["roundtrip"] = time.time() - t
        sizes = measure_sizes(Path(args.work) / "saida" / "entradas", index)
        t = time.time()
        sheets = contact_sheets(args, patched, index, args.sheets)[0] if args.sheets else []
        timings["sheets"] = time.time() - t
        write_report(args, index, deferred, changed, elapsed, roundtrip, sizes, sheets, timings)
    if "menus" in steps:
        menus_step(args, patched, fixer)
    if fixer is not None:
        buttons_report(args, fixer, steps & {"dialogue", "menus"})
        fixer.decompress.close()
    if "pack" in steps:
        pack_step(args)
        if not verify_pack(args.pack, args.game_root):
            sys.exit("[pack] verification failed")


if __name__ == "__main__":
    main()
