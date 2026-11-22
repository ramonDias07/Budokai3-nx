#!/usr/bin/env python3
"""Renders the main-menu legend ("(+)+(-) : Quick Menu") in the style of the
game's own "BACK : Help" hint and writes app/src/main_menu_legend_data.h.
Usage: python -I tools/legenda_menu.py [capture.bmp] [preview_dir]
The font is read from C:\\Windows\\Fonts at generation time and is NOT distributed;
only the rendered pixels end up in the header."""
import os, sys
from collections import Counter
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "app", "src", "main_menu_legend_data.h")
CAPTURE = (r"G:\Games\Emuladores\Nintendo\NintendoSwitch\Jogos\Meuprojeto360\Dragon Ball Z Budokai HD Collection"
           r"\B3deko\b3_deko_diag\quadros\swap_001800_1794666408"
           r"\05_saida_apresentador_00000000_1280x720_g6_rango_0_1.bmp")
PREV = r"G:\Claude\code\tmp\973281637650c3f0\dfcd690a-109a-494b-ad89-882e770c8245\scratchpad\legenda"
if len(sys.argv) > 1: CAPTURE = sys.argv[1]
if len(sys.argv) > 2: PREV = sys.argv[2]
GUEST_RIGHT, GUEST_CY = 1065.0, 700.0
FONTS = ["ARLRDBD.TTF", "ariblk.ttf", "arialbd.ttf"]
SS = 2          # supersampling factor
TEXT_H = 19.0   # cap height of the text fill in guest px (outline adds ~2 each side => ~23-24 total)
BTN = 23        # button diameter in guest px


def dist(a, b): return sum((x - y) ** 2 for x, y in zip(a, b)) ** .5
def lum(c): return .3 * c[0] + .59 * c[1] + .11 * c[2]
def pixels(im): return list(im.get_flattened_data()) if hasattr(im, "get_flattened_data") else list(im.getdata())


def measure(cap):
    im = Image.open(cap).convert("RGB")
    px = pixels(im.crop((975, 636, 1068, 680)))
    sand = Counter(p for p in px if lum(p) > 150).most_common(1)[0][0]
    rest = [p for p in px if dist(p, sand) > 40]
    outline = Counter(p for p in px if lum(p) < 70).most_common(1)[0][0]
    fill = Counter(p for p in rest if lum(p) >= 70 and p[2] < 90).most_common(1)[0][0]
    # outline thickness: dark run lengths on rows through the middle of the text
    runs = []
    for y in (652, 658, 664):
        run = 0
        for x in range(975, 1068):
            if lum(im.getpixel((x, y))) < 90: run += 1
            elif run: runs.append(run); run = 0
    thick = sorted(runs)[len(runs) // 4] / 2 if runs else 2.0
    print("sand bg", sand, "| Help fill", fill, "| outline", outline,
          "| dark run lengths (px)", sorted(set(runs)), "=> outline ~%.1f px per side" % thick)
    g = pixels(im.crop((883, 632, 935, 662)))
    nb = [p for p in g if dist(p, sand) > 40]
    light = max(nb, key=lum)
    ring = min(nb, key=lum)
    print("BACK glyph light", light, "ring", ring)
    return im, fill, outline, light, ring, thick


def find_font():
    for f in FONTS:
        p = os.path.join(r"C:\Windows\Fonts", f)
        if os.path.exists(p): return p
    raise SystemExit("no font")


def render(text_after, fill, outline, light, ring, thick, fontpath):
    S = SS
    probe = ImageFont.truetype(fontpath, 100)
    cap100 = -probe.getbbox("H", anchor="ls")[1]
    fsize = int(round(TEXT_H * S * 100 / cap100))
    font = ImageFont.truetype(fontpath, fsize)
    sw = max(1, int(round(thick * S)))
    W, Hh = 700 * S, 60 * S
    cv = Image.new("RGBA", (W, Hh), (0, 0, 0, 0))
    d = ImageDraw.Draw(cv)
    base = Hh // 2 + int(TEXT_H * S / 2)
    cy = Hh // 2
    R = BTN * S / 2

    def button(cx, sym):
        d.ellipse((cx - R, cy - R, cx + R, cy + R), fill=ring + (255,))
        r2 = R - 1.6 * S
        n = int(2 * r2) + 2
        face = Image.new("RGBA", (n, n))
        fd = ImageDraw.Draw(face)
        for yy in range(n):
            t = yy / max(1, n - 1)
            c = tuple(int(light[i] * (1.0 - .18 * t)) for i in range(3))
            fd.line([(0, yy), (n, yy)], fill=c + (255,))
        m = Image.new("L", (n, n), 0)
        ImageDraw.Draw(m).ellipse((0, 0, n - 1, n - 1), fill=255)
        cv.paste(face, (int(cx - r2), int(cy - r2)), m)
        k = 5.2 * S; t = 1.7 * S
        sc = (70, 70, 76, 255)
        d.rectangle((cx - k, cy - t, cx + k, cy + t), fill=sc)
        if sym == "+": d.rectangle((cx - t, cy - k, cx + t, cy + k), fill=sc)
        return cx + R

    def text(s, x):
        d.text((x, base), s, font=font, fill=fill + (255,), stroke_width=sw,
               stroke_fill=outline + (255,), anchor="ls")
        return x + d.textlength(s, font=font)

    x = 6 * S
    x = button(x + R, "+") + 3 * S
    x = text("+", x) + 3 * S
    x = button(x + R, "-")
    x = text(text_after, x)
    small = cv.convert("RGBa").resize((W // S, Hh // S), Image.LANCZOS).convert("RGBA")
    bb = small.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    bb = (max(0, bb[0] - 2), max(0, bb[1] - 2), min(small.width, bb[2] + 2), min(small.height, bb[3] + 2))
    return small.crop(bb)


def c_array(name, img):
    b = img.tobytes()
    lines = ["    " + ", ".join("0x%02x" % v for v in b[i:i + 24]) + "," for i in range(0, len(b), 24)]
    return "inline constexpr uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def main():
    im, fill, outline, light, ring, thick = measure(CAPTURE)
    fp = find_font()
    print("font:", fp)
    imgs = {"En": render(" : Quick Menu", fill, outline, light, ring, thick, fp),
            "Pt": render(" : Menu R\u00e1pido", fill, outline, light, ring, thick, fp)}
    os.makedirs(PREV, exist_ok=True)
    hdr = ["// Generated by tools/legenda_menu.py - do not edit by hand.",
           "// Pre-rendered main-menu legend (RGBA8, straight alpha, row-major, top-down), guest 1280x720 pixels.",
           "// The font used to render it is not distributed; only the rendered pixels are included.",
           "#pragma once", "#include <cstdint>", "", "namespace b3::legend {", ""]
    for k, img in imgs.items():
        lo = k.lower()
        hdr.append("inline constexpr uint32_t k%sWidth = %d;" % (k, img.width))
        hdr.append("inline constexpr uint32_t k%sHeight = %d;" % (k, img.height))
        hdr.append("inline constexpr float k%sGuestRight = %.1ff, k%sGuestCenterY = %.1ff;" % (k, GUEST_RIGHT, k, GUEST_CY))
        hdr.append(c_array("k%sRgba" % k, img))
        img.save(os.path.join(PREV, "legend_%s.png" % lo))
        cap = im.convert("RGBA")
        x = int(round(GUEST_RIGHT - img.width)); y = int(round(GUEST_CY - img.height / 2))
        cap.alpha_composite(img, (x, y))
        cap.save(os.path.join(PREV, "preview_%s_on_capture.png" % lo))
        cap.crop((640, 560, 1280, 720)).resize((1280, 320), Image.LANCZOS).save(
            os.path.join(PREV, "preview_%s_zoom2x.png" % lo))
        print(k, "size", img.size, "placed at", (x, y))
    hdr.append("}  // namespace b3::legend")
    with open(OUT_H, "w", newline="\n") as f: f.write("\n".join(hdr) + "\n")
    print("header", os.path.getsize(OUT_H), "bytes")


if __name__ == "__main__":
    main()
