"""PS2 button glyphs in the translated images -> Xbox 360 button glyphs taken from the game's own 360 textures.

The fan translation is a PS2 translation: it names the buttons with PS2 glyphs (x, circle, square, triangle,
L1/R1/L2/R2 labels, START/SELECT, a PS2-style d-pad) where the HD remaster shows Xbox 360 glyphs (or, in
the capsule descriptions and tutorials, the letters P/K/G/E). This module replaces them with 360 glyph art
read from data_usi.afs at run time (nothing is drawn by hand and nothing is stored in the repository):

  face buttons   data_usi #0, #AMB block 1, texture 8: the flat X, Y, B, A glyphs (42 x 42).
  LB RB LT RT    data_usi #2035 (menu legends): textures 4 (LB, RB) and 6 (LT, RT).
  START BACK     data_usi #2035 textures 8 and 14.
  d-pad          data_usi #2035 textures 21 (up/down lit) and 27 (left/right lit); one-direction and
                 neutral pads are cut from these two along the diagonals (same picture, other arms lit).

Mapping (the remaster's own choices, checked against data_spn.afs: "x Aceptar / triangle Cancelar" became
"A Aceptar / Y Cancelar" in #0, the legends of #2033/#2035 show LB/RB/LT/RT/START/BACK where the PS2 showed
L1/R1/L2/R2/START/SELECT, and the d-pad legends show the 360 d-pad):
  x -> A, circle -> B, square -> X, triangle -> Y, L1 -> LB, R1 -> RB, L2 -> LT, R2 -> RT,
  START -> START, SELECT -> BACK, d-pad -> 360 d-pad (same direction lit).
The port maps the Switch buttons by letter (Switch A = 360 A, B = B, X = X, Y = Y, L = LB, R = RB, ZL = LT,
ZR = RT, + = START, - = BACK), so the letter on each glyph is the Switch button to press.

Three kinds of images are handled:
  legends   #2035 textures 3..61 and #2033 texture 24 ("<glyph> : text"): the glyph part (left of the
            colon) is taken from the data_spn texture the container is built on (same index, same picture
            in 360 art); the translated text right of the colon is kept.
  inline    glyphs the translators drew inside text (capsule descriptions, tutorials, help pages, the
            command-list atlases): detected in the PS2 image, erased (transparent, or the surrounding flat
            colour on opaque art) and replaced by the 360 glyph scaled to the PS2 glyph's height, centred.
  text      memory card messages: see ptbr/memoria.py.
"""

from dataclasses import dataclass, field

import numpy as np

from . import amb, x360tex

FACE_360 = {"cross": "A", "circle": "B", "square": "X", "triangle": "Y"}
SHOULDER_360 = {"L1": "LB", "R1": "RB", "L2": "LT", "R2": "RT"}

# Legend textures: (entry, block, texture) -> x where the text part starts on the 360 texture (the colon).
LEGEND_SPLIT_2035 = 178   # 360 #2035: glyphs end before x 178, the colon is at 179..188
LEGENDS = {(2035, -1, k): LEGEND_SPLIT_2035 for k in range(3, 62)}
LEGENDS[(2033, -1, 24)] = 118   # "SELECT : Ajuda" -> BACK; 360 colon at 120..130
# #0 block 8: "x Aceitar" / "triangle Cancelar" (the 360 shows "A Aceptar" / "Y Cancelar"); the translators
# redrew the whole picture, so the disc is taken from the 360 picture without the "unchanged" check.
LEGENDS[(0, 8, 49)] = 28
LEGENDS[(0, 8, 50)] = 28
LEGEND_FORCE = {(0, 8, 49), (0, 8, 50)}
# Illustrations with PS2 buttons drawn in the art, taken from the 360 picture (data_spn, no text in these
# rows): help page #1980 texture 6, the combo panel (PS2 square/circle discs; the 360 shows P/E discs).
# (entry, block, texture) -> (first row, end row) in 360 pixels.
ART_ROWS = {(1980, -1, 6): (322, 602)}


@dataclass
class Edit:
    kind: str            # "face", "dpad", "shoulder", "legend"
    ps2: str             # PS2 glyph name
    x360: str            # 360 glyph used
    bbox: tuple          # (x0, y0, x1, y1) in the final image
    note: str = ""


@dataclass
class Result:
    image: np.ndarray
    edits: list = field(default_factory=list)


# ---------------------------------------------------------------------------------------------------
# 360 glyph art
# ---------------------------------------------------------------------------------------------------

def _tex(data, block, index):
    if block >= 0:
        data = amb.parse(data, ">").blocks[block]
    t = x360tex.parse(data).textures[index]
    return x360tex.decode(t)[:t.height, :t.width]


def _trim(img):
    ys, xs = np.nonzero(img[..., 3] > 8)
    return img[ys.min():ys.max() + 1, xs.min():xs.max() + 1].copy()


class Atlas:
    """360 glyph pictures (RGBA, straight alpha, trimmed) by name."""

    def __init__(self, usi, decompress):
        self.glyphs = {}
        faces = _tex(decompress(usi.read(0)), 1, 8)
        for name, x0 in (("X", 3), ("Y", 51), ("B", 99), ("A", 147)):
            self.glyphs[name] = _trim(faces[97:143, x0 - 2:x0 + 44])
        legend = decompress(usi.read(2035))
        lb_rb = _tex(legend, -1, 4)
        self.glyphs["LB"] = _trim(lb_rb[:, 59:115])
        self.glyphs["RB"] = _trim(lb_rb[:, 117:173])
        lt_rt = _tex(legend, -1, 6)
        self.glyphs["LT"] = _trim(lt_rt[:, 68:106])
        self.glyphs["RT"] = _trim(lt_rt[:, 121:164])
        self.glyphs["START"] = _trim(_tex(legend, -1, 8)[:, 101:165])
        self.glyphs["BACK"] = _trim(_tex(legend, -1, 14)[:, 106:161])
        vertical = _tex(legend, -1, 21)[:, 117:170]
        horizontal = _tex(legend, -1, 27)[:, 117:170]
        self._dpads(vertical, horizontal)

    def _dpads(self, vertical, horizontal):
        h = min(vertical.shape[0], horizontal.shape[0])
        vertical, horizontal = vertical[:h], horizontal[:h]
        ys, xs = np.nonzero((vertical[..., 3] > 8) | (horizontal[..., 3] > 8))
        cy, cx = (ys.min() + ys.max()) / 2, (xs.min() + xs.max()) / 2
        yy, xx = np.mgrid[0:h, 0:vertical.shape[1]]
        dy, dx = yy - cy, xx - cx
        horiz = np.abs(dx) > np.abs(dy)
        wedges = {"up": ~horiz & (dy < 0), "down": ~horiz & (dy >= 0),
                  "left": horiz & (dx < 0), "right": horiz & (dx >= 0)}
        neutral = np.where(horiz[..., None], vertical, horizontal)   # the arms that are not lit in each
        self.glyphs["DPAD"] = _trim(neutral)
        for name, mask in wedges.items():
            lit = horizontal if name in ("left", "right") else vertical
            self.glyphs["DPAD_" + name.upper()] = _trim(np.where(mask[..., None], lit, neutral))

    def get(self, name, height):
        """Glyph scaled to `height` pixels (premultiplied Lanczos), straight alpha."""
        from PIL import Image
        src = self.glyphs[name].astype(np.float32)
        height = max(4, int(round(height)))
        width = max(4, int(round(src.shape[1] * height / src.shape[0])))
        pm = np.concatenate([src[..., :3] * src[..., 3:] / 255.0, src[..., 3:]], axis=-1)
        out = np.stack([np.asarray(Image.fromarray(pm[..., c], mode="F").resize((width, height), Image.LANCZOS))
                        for c in range(4)], axis=-1)
        out = np.clip(out, 0, 255)
        alpha = out[..., 3:]
        rgb = np.where(alpha > 0.5, out[..., :3] * 255.0 / np.maximum(alpha, 1e-3), 0)
        return np.concatenate([np.clip(rgb, 0, 255), alpha], axis=-1).astype(np.uint8)


# ---------------------------------------------------------------------------------------------------
# Detection on the PS2 image
# ---------------------------------------------------------------------------------------------------

def _label(mask):
    """8-connected components -> list of (y0, x0, y1, x1, count)."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    out = []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        stack = [(y, x)]
        seen[y, x] = True
        y0 = y1 = y
        x0 = x1 = x
        n = 0
        while stack:
            cy, cx = stack.pop()
            n += 1
            y0, y1, x0, x1 = min(y0, cy), max(y1, cy), min(x0, cx), max(x1, cx)
            for ny in (cy - 1, cy, cy + 1):
                for nx in (cx - 1, cx, cx + 1):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        out.append((int(y0), int(x0), int(y1) + 1, int(x1) + 1, n))
    return out


def _erode(mask, n=1):
    for _ in range(n):
        p = np.pad(mask, 1)
        mask = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return mask


def _dilate(mask, n=1):
    for _ in range(n):
        p = np.pad(mask, 1)
        mask = p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
    return mask


def _hue(rgb):
    import colorsys
    r, g, b = (rgb.mean(0) / 255.0).tolist()
    return colorsys.rgb_to_hsv(r, g, b)[0] * 360


def _symbol(pixels):
    """PS2 face symbol from its coloured pixels (n x 3): pink square, red circle, teal triangle, blue x."""
    hue = _hue(pixels)
    r, g, b = pixels.astype(float).mean(0)
    if 300 <= hue < 352:
        return "square"
    if hue >= 352 or hue < 25:
        return "circle"
    if 120 <= hue < 240:
        return "triangle" if g > b + 12 else "cross"
    return None


@dataclass
class Glyph:
    kind: str                  # "face", "dpad", "shoulder"
    name: str                  # PS2 name: cross/circle/square/triangle, up/down/left/right/none, L1/R1/L2/R2
    box: tuple                 # (x0, y0, x1, y1) of the whole glyph (with its halo/outline) in the PS2 image
    footprint: np.ndarray = None   # bool mask (PS2 image size) of the pixels to erase


def find_faces(im):
    """Face-button glyphs drawn by the translators: a coloured symbol on a grey disc with a thin dark
    outline. Disc centres are the deepest points of the solid disc area (repeated erosion)."""
    rgb = im[..., :3].astype(int)
    a = im[..., 3]
    sat = rgb.max(-1) - rgb.min(-1)
    grey = (a > 200) & (sat < 22) & (rgb.max(-1) > 60) & (rgb.max(-1) < 140)
    symbol = (a > 150) & (sat > 50) & (rgb.max(-1) > 140)
    solid = (a > 150) & (rgb.max(-1) > 60)
    depth = np.zeros(a.shape, dtype=np.int32)
    m = solid
    for _ in range(18):
        m = _erode(m, 1)
        if not m.any():
            break
        depth += m
    out = []
    H, W = a.shape
    for y0, x0, y1, x1, n in _label(depth >= 3):
        # the disc reaches 3 px past the core (the erosion depth), the halo 2 more
        ry, rx = (y1 - y0) / 2 + 3, (x1 - x0) / 2 + 3
        if not (4 <= ry <= 16 and 4 <= rx <= 16) or max(ry, rx) > 1.6 * min(ry, rx):
            continue
        cy, cx = (y0 + y1 - 1) / 2, (x0 + x1 - 1) / 2
        Y0, Y1 = int(max(0, cy - ry - 4)), int(min(H, cy + ry + 5))
        X0, X1 = int(max(0, cx - rx - 4)), int(min(W, cx + rx + 5))
        yy, xx = np.mgrid[Y0:Y1, X0:X1]
        def ellipse(grow):
            return ((yy - cy) / (ry + grow)) ** 2 + ((xx - cx) / (rx + grow)) ** 2 <= 1
        inner = ellipse(-0.5)
        if solid[Y0:Y1, X0:X1][inner].mean() < 0.85:
            continue
        coloured = inner & symbol[Y0:Y1, X0:X1]
        if coloured.sum() < 6 or coloured.sum() > 0.5 * inner.sum():
            continue
        if (grey[Y0:Y1, X0:X1] & inner).sum() < 0.3 * inner.sum():
            continue
        core = ellipse(-min(ry, rx) * 0.22) & symbol[Y0:Y1, X0:X1]   # the symbol, away from the rim
        if core.sum() < 6:
            continue
        name = _symbol_shape(rgb[Y0:Y1, X0:X1], core, cy - Y0, cx - X0)
        if name is None:
            continue
        foot = np.zeros(a.shape, dtype=bool)
        foot[Y0:Y1, X0:X1] = ellipse(2.6)
        out.append(Glyph("face", name, (cx - rx - 1.5, cy - ry - 1.5, cx + rx + 2.5, cy + ry + 2.5), foot))
    return out


def _symbol_shape(rgb, mask, cy, cx):
    """PS2 symbol from colour (teal triangle, blue x) and, for the pink/red ones, shape (square/circle)."""
    pixels = rgb[mask]
    hue = _hue(pixels)
    r, g, b = pixels.astype(float).mean(0)
    if 120 <= hue < 250:
        return "triangle" if g > b + 12 else "cross"
    if not (hue >= 290 or hue < 30):
        return None
    ys, xs = np.nonzero(mask)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    qy, qx = max(1, (y1 - y0 + 1) // 4), max(1, (x1 - x0 + 1) // 4)
    corner = (((ys - y0 < qy) | (y1 - ys < qy)) & ((xs - x0 < qx) | (x1 - xs < qx))).mean()
    # a square outline fills the corners of its bounding box, a circle leaves them empty
    return "square" if corner > 0.14 else "circle"


def _read_label(ink, box):
    """L1/R1/L2/R2 from the letter pixels (bool mask, image size) inside a label box, or None."""
    x0, y0, x1, y1 = box
    sub = ink[y0:y1, x0:x1]
    comps = sorted((c for c in _label(sub) if c[4] >= 3), key=lambda c: c[1])
    if len(comps) >= 2:
        parts = [sub[comps[0][0]:comps[0][2], comps[0][1]:comps[0][3]],
                 sub[comps[-1][0]:comps[-1][2], comps[-1][1]:comps[-1][3]]]
    elif len(comps) == 1:
        c = comps[0]
        piece = sub[c[0]:c[2], c[1]:c[3]]
        w = piece.shape[1]
        if w < 6:
            return None
        middle = range(w // 3, 2 * w // 3 + 1)
        split = min(middle, key=lambda col: piece[:, col].sum())
        parts = [piece[:, :split], piece[:, split + 1:]]
    else:
        return None

    def tight(m):
        ys, xs = np.nonzero(m)
        if len(ys) == 0:
            return None
        return m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]

    first, second = tight(parts[0]), tight(parts[1])
    if first is None or second is None or first.shape[0] < 5 or second.shape[0] < 5:
        return None
    fh, fw = first.shape
    # R has ink in the upper right (the bowl); L only has the stem there
    upper_right = first[:max(1, fh * 2 // 5), fw // 2:].mean() if fw > 2 else 0
    letter = "R" if upper_right > 0.3 else "L"
    sh, sw = second.shape
    centre = second[:, sw // 3:max(sw // 3 + 1, 2 * sw - sw * 4 // 3)].sum() / max(1, second.sum())
    middle = second[:, max(0, (sw - max(2, sw // 3)) // 2):(sw + max(2, sw // 3)) // 2 + 1]
    # 1: most of the ink in the central column; 2: ink spread (foot, diagonal, top bow)
    digit = "1" if (middle.sum() / max(1, second.sum()) > 0.5 or sw < 0.5 * sh) else "2"
    return letter + digit


def find_shoulder_boxes(im):
    """L1/R1/L2/R2 drawn by the translators: dark letters in a white rounded box with a dark outline."""
    rgb = im[..., :3].astype(int)
    a = im[..., 3]
    white = (a > 150) & (rgb.min(-1) > 190)
    H, W = white.shape
    out = []
    if (a > 150).mean() > 0.6:
        return out          # opaque pages (help pages #1980): only words in white pills there, no labels
    for y0, x0, y1, x1, n in _label(white):
        h, w = y1 - y0, x1 - x0
        if not (7 <= h <= 24 and 12 <= w <= 48 and 1.2 <= w / h <= 3.4) or n / (h * w) < 0.5:
            continue
        Y0, Y1, X0, X1 = max(0, y0 - 1), min(H, y1 + 1), max(0, x0 - 1), min(W, x1 + 1)
        ring = np.ones((Y1 - Y0, X1 - X0), dtype=bool)
        ring[y0 - Y0:y1 - Y0, x0 - X0:x1 - X0] = False
        outline = (a[Y0:Y1, X0:X1] > 150) & ~white[Y0:Y1, X0:X1]
        if ring.sum() == 0 or (outline & ring).sum() / ring.sum() < 0.6:
            continue
        inner = (a[y0:y1, x0:x1] > 150) & ~white[y0:y1, x0:x1]
        enclosed = [c for c in _label(inner) if c[0] > 0 and c[1] > 0 and c[2] < h and c[3] < w and c[4] >= 4]
        if not enclosed or len(enclosed) > 3 or sum(c[4] for c in enclosed) < max(20, 0.15 * h * w):
            continue
        ey0, ex0 = min(c[0] for c in enclosed), min(c[1] for c in enclosed)
        ey1, ex1 = max(c[2] for c in enclosed), max(c[3] for c in enclosed)
        # "L1"/"R2": two characters filling most of the box (letters of a word are smaller and more)
        if (ex1 - ex0) < 0.45 * w or (ey1 - ey0) < 0.45 * h:
            continue
        ink = np.zeros(inner.shape, dtype=bool)
        for c in enclosed:
            ink[c[0]:c[2], c[1]:c[3]] |= inner[c[0]:c[2], c[1]:c[3]]
        name = _read_label(inner_mask(ink, y0, x0, a.shape), (x0, y0, x1, y1))
        if name is None:
            continue
        b = (max(0, x0 - 3), max(0, y0 - 3), min(W, x1 + 3), min(H, y1 + 3))
        foot = np.zeros(a.shape, dtype=bool)
        foot[b[1]:b[3], b[0]:b[2]] = True
        out.append(Glyph("shoulder", name, b, foot))
    return out


def inner_mask(inner, y0, x0, shape):
    m = np.zeros(shape, dtype=bool)
    m[y0:y0 + inner.shape[0], x0:x0 + inner.shape[1]] = inner
    return m


def _box_sums(mask):
    sat = np.zeros((mask.shape[0] + 1, mask.shape[1] + 1), dtype=np.int32)
    sat[1:, 1:] = mask.astype(np.int32).cumsum(0).cumsum(1)
    return sat


def _box(sat, y0, x0, y1, x1, th, tw):
    """Sum of mask[i + y0 : i + y1, j + x0 : j + x1] for every window origin (i, j) of a th x tw window."""
    H, W = sat.shape[0] - 1, sat.shape[1] - 1
    hh, ww = H - th + 1, W - tw + 1
    return (sat[y1:y1 + hh, x1:x1 + ww] - sat[y0:y0 + hh, x1:x1 + ww]
            - sat[y1:y1 + hh, x0:x0 + ww] + sat[y0:y0 + hh, x0:x0 + ww])


MID, CORNER, OUTLINE = 0.18, 0.3, 0.7


EXEMPLAR_MIN = 0.5

# The translators pasted the same few d-pad pictures everywhere: (PS2 DATA_SPN entry, texture, x, y, w, h)
# of one copy of each; their rotations and mirror images are used too. A d-pad-shaped window must match
# one of them (outline and inner lines; the lit arm may differ) to be replaced.
DPAD_EXEMPLARS = ((2093, 3, 153, 2, 17, 18), (2093, 3, 98, 42, 17, 20), (2003, 5, 131, 46, 17, 19),
                  (2002, 10, 329, 22, 19, 22), (2011, 7, 220, 31, 22, 26), (2011, 7, 241, 31, 22, 26))


def dpad_exemplars(read_image):
    """[(opaque, dark)] masks of the exemplar pads; read_image(entry, texture) -> PS2 RGBA picture."""
    out = []
    for entry, tex, x, y, w, h in DPAD_EXEMPLARS:
        im = read_image(entry, tex)[y:y + h, x:x + w]
        rgb, a = im[..., :3].astype(int), im[..., 3]
        op, dk = a > 150, (a > 150) & (rgb.mean(-1) < 70)
        for k in range(4):
            for flip in (False, True):
                o, d = np.rot90(op, k), np.rot90(dk, k)
                if flip:
                    o, d = o[:, ::-1], d[:, ::-1]
                out.append((np.ascontiguousarray(o), np.ascontiguousarray(d)))
    return out


def _exemplar_score(opaque, dark, y, x, th, tw, exemplars):
    """Best overlap (intersection over union) of the window's dark pixels (outline and inner lines) with
    those of an exemplar of about the same size, trying small shifts. The lit arm and the coloured halo
    some copies carry do not count. Real pads score 0.59-1.0, letters of outlined fonts 0.27-0.40."""
    H, W = dark.shape
    best = 0.0
    for eo, ed in exemplars:
        eh, ew = ed.shape
        if abs(eh - th) > 3 or abs(ew - tw) > 3:
            continue
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                y0 = y + (th - eh) // 2 + dy
                x0 = x + (tw - ew) // 2 + dx
                if y0 < 0 or x0 < 0 or y0 + eh > H or x0 + ew > W:
                    continue
                cd = dark[y0:y0 + eh, x0:x0 + ew]
                union = (cd | ed).sum()
                if union:
                    best = max(best, float((cd & ed).sum() / union))
    return best


def find_dpads(im, exemplars=None):
    """The translators' d-pad glyph: a plus-shaped cross (black and white) whose lit arm is filled
    white. Found by its geometry: both centre bands solid, the four corners empty."""
    rgb = im[..., :3].astype(int)
    a = im[..., 3]
    H, W = a.shape
    opaque = a > 150
    opaque_raw = opaque
    if opaque.mean() > 0.6:                      # glyph on opaque art: use "differs from the backdrop"
        bg = np.median(rgb[opaque], axis=0)
        opaque = opaque & (np.abs(rgb - bg).max(-1) > 40)
    dark = (a > 150) & (rgb.mean(-1) < 70)
    white = (a > 150) & (rgb.min(-1) > 200)
    mid = (a > 150) & ~dark & ~white
    S, D, Wt, Mid = _box_sums(opaque), _box_sums(dark), _box_sums(white), _box_sums(mid)
    found = []
    for th in range(12, 29):
        for tw in sorted({round(th * r) for r in (0.75, 0.85, 0.95, 1.05)}):
            if th > H or tw > W:
                continue
            ay0, ay1 = round(th * 0.33), round(th * 0.67)
            ax0, ax1 = round(tw * 0.33), round(tw * 0.67)
            cy, cx = max(2, round(th * 0.18)), max(2, round(tw * 0.18))
            vband = _box(S, 0, ax0, th, ax1, th, tw) / (th * (ax1 - ax0))
            hband = _box(S, ay0, 0, ay1, tw, th, tw) / ((ay1 - ay0) * tw)
            corners = [_box(S, 0, 0, cy, cx, th, tw), _box(S, 0, tw - cx, cy, tw, th, tw),
                       _box(S, th - cy, 0, th, cx, th, tw), _box(S, th - cy, tw - cx, th, tw, th, tw)]
            corner = np.maximum.reduce(corners) / (cy * cx)
            n = th * tw
            dark_f = _box(D, 0, 0, th, tw, th, tw) / n
            white_f = _box(Wt, 0, 0, th, tw, th, tw) / n
            # the four arm ends are drawn in one outline colour (black: small style, white: big style)
            # (first or second pixel row/column of each end: some copies carry a 1 px anti-aliased halo)
            ends = {}
            for name, M in (("dark", D), ("white", Wt)):
                ends[name] = (np.maximum(_box(M, 0, ax0, 1, ax1, th, tw), _box(M, 1, ax0, 2, ax1, th, tw))
                              + np.maximum(_box(M, th - 1, ax0, th, ax1, th, tw), _box(M, th - 2, ax0, th - 1, ax1, th, tw))
                              + np.maximum(_box(M, ay0, 0, ay1, 1, th, tw), _box(M, ay0, 1, ay1, 2, th, tw))
                              + np.maximum(_box(M, ay0, tw - 1, ay1, tw, th, tw), _box(M, ay0, tw - 2, ay1, tw - 1, th, tw))
                              ) / (2 * (ax1 - ax0) + 2 * (ay1 - ay0))
            outline = np.maximum(ends["dark"], ends["white"])
            ok = ((vband >= 0.85) & (hband >= 0.85) & (corner <= CORNER) & (dark_f >= 0.12) & (white_f >= 0.08)
                  & (outline >= OUTLINE) & (_box(Mid, 0, 0, th, tw, th, tw) / n >= MID))
            score = (vband + hband) / 2 - corner + 0.002 * th
            for y, x in zip(*np.nonzero(ok)):
                found.append((float(score[y, x]), int(y), int(x), th, tw))
    found.sort(key=lambda f: -f[0])
    out = []
    taken = np.zeros(a.shape, dtype=bool)
    rejected = np.zeros(a.shape, dtype=bool)
    for score, y, x, th, tw in found:
        # pads may touch: reject a window only when it overlaps a kept one by more than a few pixels
        if taken[y + 3:y + th - 3, x + 3:x + tw - 3].any():
            continue
        yy, xx = np.mgrid[0:th, 0:tw]
        cross = ((xx >= tw * 0.3) & (xx < tw * 0.7)) | ((yy >= th * 0.3) & (yy < th * 0.7))
        inside = _erode(cross, 2)
        lit = white[y:y + th, x:x + tw] & inside
        arms = {"up": lit[:th // 3], "down": lit[2 * th // 3:],
                "left": lit[:, :tw // 3], "right": lit[:, 2 * tw // 3:]}
        areas = {"up": inside[:th // 3].sum(), "down": inside[2 * th // 3:].sum(),
                 "left": inside[:, :tw // 3].sum(), "right": inside[:, 2 * tw // 3:].sum()}
        counts = {k: int(v.sum()) for k, v in arms.items()}
        best = max(counts, key=counts.get)
        cy, cx = y + th // 2, x + tw // 2
        if rejected[cy, cx]:
            continue            # a window centred here was already compared (the score tries shifts/sizes)
        if exemplars is not None and _exemplar_score(opaque_raw, dark, y, x, th, tw, exemplars) < EXEMPLAR_MIN:
            rejected[max(0, cy - 2):cy + 3, max(0, cx - 2):cx + 3] = True
            continue
        name = best if counts[best] >= 0.35 * max(1, areas[best]) else "none"
        taken[y:y + th, x:x + tw] = True
        foot = np.zeros(a.shape, dtype=bool)
        foot[y:y + th, x:x + tw] = _dilate(cross, 1)
        out.append(Glyph("dpad", name, (x, y, x + tw, y + th), foot))
    return out


# The combat command-list atlases (data_usi #2 block 1 texture 1, #3 block 1 texture 0, #17 block 0
# texture 0): the translators replaced the P/K/G/E letters with bare PS2 symbols on the dark pills and the
# shortcut cells with L1/R1/L2/R2 labels (light letters in a dark box).
ATLASES = {(2, 1, 1), (3, 1, 0), (17, 0, 0)}


def find_bare_symbols(im):
    rgb = im[..., :3].astype(int)
    a = im[..., 3]
    sat = rgb.max(-1) - rgb.min(-1)
    coloured = (a > 150) & (sat > 60)
    out = []
    for y0, x0, y1, x1, n in _label(_dilate(coloured, 1)):
        h, w = y1 - y0, x1 - x0
        if not (7 <= h <= 20 and 7 <= w <= 20) or n < 15:
            continue
        name = _symbol(rgb[y0:y1, x0:x1][coloured[y0:y1, x0:x1]])
        if name is None:
            continue
        foot = np.zeros(a.shape, dtype=bool)
        foot[y0:y1, x0:x1] = True
        out.append(Glyph("face", name, (x0 - 1, y0 - 1, x1 + 1, y1 + 1), foot))
    return out


def find_dark_labels(im):
    rgb = im[..., :3].astype(int)
    a = im[..., 3]
    light = (a > 150) & (rgb.min(-1) > 150)
    out = []
    for y0, x0, y1, x1, n in _label(a > 40):
        h, w = y1 - y0, x1 - x0
        if not (12 <= h <= 24 and 18 <= w <= 34):
            continue
        letters = light[y0:y1, x0:x1]
        if (a[y0:y1, x0:x1] > 40).mean() < 0.85:
            continue
        if letters.sum() < 15 or (rgb[y0:y1, x0:x1].mean(-1)[a[y0:y1, x0:x1] > 150] < 90).mean() < 0.4:
            continue
        name = _read_label(inner_mask(letters, y0, x0, a.shape), (x0, y0, x1, y1))
        if name is None:
            continue
        foot = np.zeros(a.shape, dtype=bool)
        foot[y0:y1, x0:x1] = a[y0:y1, x0:x1] > 0
        out.append(Glyph("shoulder", name, (x0, y0, x1, y1), foot))
    return out


def find(im, atlas_texture=False, review=None, exemplars=None):
    """Every PS2 glyph of a translated PS2 picture. D-pad-like shapes with no lit arm are not replaced
    (a neutral pad is rare in the text and letters of outlined fonts can look like one): they are
    appended to `review` instead."""
    if atlas_texture:
        return find_bare_symbols(im) + find_dark_labels(im)
    pads = find_dpads(im, exemplars)
    if review is not None:
        review.extend(g for g in pads if g.name == "none")
    return find_faces(im) + find_shoulder_boxes(im) + [g for g in pads if g.name != "none"]


# ---------------------------------------------------------------------------------------------------
# Replacement
# ---------------------------------------------------------------------------------------------------

def _background(image, mask):
    """(transparent?, fill colour) around `mask`."""
    ring = _dilate(mask, 3) & ~_dilate(mask, 1)
    if not ring.any():
        return True, np.zeros(4, dtype=np.uint8)
    alpha = image[..., 3][ring]
    if (alpha < 40).mean() > 0.5:
        return True, np.zeros(4, dtype=np.uint8)
    opaque = image[ring][alpha >= 40]
    return False, np.median(opaque, axis=0).astype(np.uint8)


def _over(dst, src, x, y):
    """Straight-alpha `src` over `dst` at (x, y), clipped."""
    h, w = src.shape[:2]
    H, W = dst.shape[:2]
    sx0, sy0 = max(0, -x), max(0, -y)
    dx0, dy0 = max(0, x), max(0, y)
    sx1, sy1 = min(w, W - x), min(h, H - y)
    if sx1 <= sx0 or sy1 <= sy0:
        return
    s = src[sy0:sy1, sx0:sx1].astype(np.float32)
    d = dst[dy0:dy0 + (sy1 - sy0), dx0:dx0 + (sx1 - sx0)].astype(np.float32)
    sa, da = s[..., 3:] / 255.0, d[..., 3:] / 255.0
    oa = sa + da * (1 - sa)
    rgb = (s[..., :3] * sa + d[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-6)
    out = np.concatenate([rgb, oa * 255.0], axis=-1)
    dst[dy0:dy0 + (sy1 - sy0), dx0:dx0 + (sx1 - sx0)] = np.clip(np.rint(out), 0, 255).astype(np.uint8)


def _scale_mask(mask, sx, sy, shape):
    if sx == 1 and sy == 1:
        return mask
    ys, xs = np.nonzero(mask)
    out = np.zeros(shape, dtype=bool)
    for y, x in zip(ys, xs):
        out[int(y * sy):int(np.ceil((y + 1) * sy)), int(x * sx):int(np.ceil((x + 1) * sx))] = True
    return out


def target_name(glyph):
    if glyph.kind == "face":
        return FACE_360[glyph.name]
    if glyph.kind == "shoulder":
        return SHOULDER_360[glyph.name]
    return "DPAD" if glyph.name == "none" else "DPAD_" + glyph.name.upper()


def replace(image, glyphs, atlas, scale=(1.0, 1.0)):
    """Erases each glyph's footprint in `image` (final size; the glyphs were found on the PS2 image,
    `scale` = final / PS2) and pastes the 360 glyph. Returns the list of Edit."""
    sx, sy = scale
    edits = []
    for g in glyphs:
        foot = _scale_mask(g.footprint, sx, sy, image.shape[:2])
        transparent, fill = _background(image, foot)
        image[foot] = fill if not transparent else 0
    for g in glyphs:
        x0, y0, x1, y1 = g.box
        x0, x1, y0, y1 = x0 * sx, x1 * sx, y0 * sy, y1 * sy
        name = target_name(g)
        h = (y1 - y0)
        if name in ("LT", "RT"):
            h *= 1.35       # the trigger icons are tall and narrow: a bit taller than the PS2 label box
        art = atlas.get(name, h)
        if art.shape[1] > (x1 - x0) * 1.6 and g.kind != "shoulder":
            art = atlas.get(name, h * (x1 - x0) * 1.6 / art.shape[1])
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        px, py = int(round(cx - art.shape[1] / 2)), int(round(cy - art.shape[0] / 2))
        _over(image, art, px, py)
        edits.append(Edit(g.kind, g.name, name, (px, py, px + art.shape[1], py + art.shape[0])))
    return edits


def legend(image, base_image, split, ps2_translated, ps2_clean, scale):
    """Legend texture: columns left of `split` (360 pixels) come from the 360 base texture (data_spn,
    360 glyph art), the rest stays as translated. Only done when the translators kept the PS2 glyph part
    where the clean PS2 picture has it (so the layout is the original one)."""
    ps2_split = int(split / scale[0])
    if ps2_translated.shape != ps2_clean.shape:
        return None
    # same glyph layout (the translators re-quantised the pictures, so the pixels differ a little):
    # the opaque parts left of the colon must overlap almost entirely
    a = ps2_translated[:, :max(1, ps2_split - 3), 3] > 100
    b = ps2_clean[:, :max(1, ps2_split - 3), 3] > 100
    union = (a | b).sum()
    if union and (a & b).sum() / union < 0.9:
        return None
    h = min(image.shape[0], base_image.shape[0])
    image[:, :split] = 0
    image[:h, :split] = base_image[:h, :split]
    return [Edit("legend", "PS2 legend glyphs", "360 glyphs from data_spn", (0, 0, split, h))]


def erase(image, glyphs, scale=(1.0, 1.0)):
    """Only the erase half of replace() (used on a PS2 source before it is upscaled)."""
    sx, sy = scale
    for g in glyphs:
        foot = _scale_mask(g.footprint, sx, sy, image.shape[:2])
        transparent, fill = _background(image, foot)
        image[foot] = fill if not transparent else 0


# ---------------------------------------------------------------------------------------------------
# Pipeline hooks (tools/traducao_ptbr.py)
# ---------------------------------------------------------------------------------------------------

class Fixer:
    """Applies the glyph replacement and the memory card edits to the dialogue entries and the menu jobs,
    and keeps a record of every change (botoes.json, contact sheets)."""

    def __init__(self, usi, decompress, clean_spn, translated_spn):
        from . import memoria, ps2tex
        self.memoria, self.ps2tex = memoria, ps2tex
        self.atlas = Atlas(usi, decompress)
        self.clean_spn = clean_spn

        def read_image(entry, tex):
            data = translated_spn.read(entry, sector_slack=True)
            return ps2tex.decode(data, ps2tex.parse(data)[tex])
        self.exemplars = dpad_exemplars(read_image)
        self.records = []      # one dict per edited texture
        self.review = []       # things left for a person to look at
        self.sheet_rows = []   # (label, before, after, (entry, block, tex) of the 360 English picture)
        self._templates = {}   # entry -> "PS2" stroke templates of the memory card pictures
        self._pending = {}     # id(job) -> (glyphs, memory edits, original source)

    # --- shared -------------------------------------------------------------------------------------
    def _memory(self, entry, index, image, siblings):
        wanted = self.memoria.ENTRIES.get(entry, ())
        if wanted is not None and index not in wanted:
            return []
        if entry not in self._templates:
            pages = (self.memoria.Page(s) for s in siblings)
            self._templates[entry] = [t for t in (self.memoria.template(p) for p in pages) if t is not None][:3]
        return self.memoria.edit(image, self._templates[entry])

    def _record(self, file, entry, block, tex, ps2_index, kind, glyphs, memory, edits, extra=None):
        if not glyphs and not memory and not edits:
            return
        rec = {"file": file, "entry": entry, "block": block, "tex": tex, "ps2": ps2_index, "kind": kind,
               "glyphs": [{"kind": e.kind, "ps2": e.ps2, "x360": e.x360, "bbox": [int(v) for v in e.bbox]}
                          for e in edits],
               "memory": [{"removed": k, "bbox": [int(v) for v in box]} for k, box, _ in memory]}
        if extra:
            rec.update(extra)
        self.records.append(rec)

    def _review(self, where, review):
        for g in review:
            self.review.append(f"{where}: d-pad-like shape without a lit arm at {tuple(int(v) for v in g.box)} "
                               "(not replaced)")

    # --- dialogue entries (#AMT -> #AZT, same size) --------------------------------------------------
    def dialogue(self, entry, index, image, siblings):
        """`image`: PS2 picture on the 360 canvas, edited in place."""
        before = image.copy()
        memory = self._memory(entry, index, image, siblings) if entry in self.memoria.ENTRIES else []
        review = []
        glyphs = find(image, False, review, self.exemplars)
        self._review(f"data_usi #{entry} t{index}", review)
        edits = replace(image, glyphs, self.atlas) if glyphs else []
        self._record("data_usi", entry, -1, index, index, "dialogue", glyphs, memory, edits)
        if glyphs or memory:
            self.sheet_rows.append((f"data_usi #{entry} t{index} dialogue", before, image.copy(), (entry, -1, index)))

    # --- menu jobs (menus.build) ----------------------------------------------------------------------
    def menu_pre(self, container, job):
        """Before upscaling: memory card edits and erasing the PS2 glyphs on the PS2 source."""
        if job.file != "data_usi":
            return
        original = job.source.copy()
        key = (job.entry, job.block, job.tex)
        memory = []
        if job.entry in self.memoria.ENTRIES:
            memory = self._memory(job.entry, job.ps2_index, job.source, [j.source for j in container.jobs])
        glyphs = []
        if key not in LEGENDS:
            review = []
            glyphs = find(job.source, key in ATLASES, review, self.exemplars)
            self._review(f"data_usi #{job.entry} b{job.block} t{job.tex}", review)
            if glyphs:
                job.source = job.source.copy()
                erase(job.source, glyphs)
        if glyphs or memory or key in LEGENDS or key in ART_ROWS:
            self._pending[id(job)] = (glyphs, memory, original)

    def menu_post(self, container, job, image):
        """After rendering at the final size: legend glyph art, or the 360 glyphs pasted."""
        pending = self._pending.pop(id(job), None)
        if pending is None:
            return image
        glyphs, memory, original = pending
        image = image.copy()
        key = (job.entry, job.block, job.tex)
        scale = (image.shape[1] / job.source.shape[1], image.shape[0] / job.source.shape[0])
        edits = []
        if key in LEGENDS:
            edits = self._legend(job, image, LEGENDS[key], original, scale)
        elif glyphs:
            edits = replace(image, glyphs, self.atlas, scale)
        if key in ART_ROWS:
            y0, y1 = ART_ROWS[key]
            base = x360tex.decode(job.template)[:job.template.height, :job.template.width]
            if base.shape[:2] == image.shape[:2]:
                image[y0:y1] = base[y0:y1]
                edits.append(Edit("art", "PS2 buttons in the illustration", "360 illustration (data_spn)",
                                  (0, y0, image.shape[1], y1)))
            else:
                self.review.append(f"data_usi #{job.entry} t{job.tex}: illustration not replaced (size differs)")
        self._record(job.file, job.entry, job.block, job.tex, job.ps2_index, job.kind, glyphs, memory, edits)
        if edits or memory:
            label = (f"data_usi #{job.entry}" + (f" b{job.block}" if job.block >= 0 else "") +
                     f" t{job.tex} {job.kind} {original.shape[1]}x{original.shape[0]}->{image.shape[1]}x{image.shape[0]}")
            self.sheet_rows.append((label, original, image, (job.entry, job.block, job.tex)))
        return image

    def _legend(self, job, image, split, original, scale):
        base = x360tex.decode(job.template)[:job.template.height, :job.template.width]
        if (job.entry, job.block, job.tex) in LEGEND_FORCE:
            return legend(image, base, split, original, original, scale)
        try:
            data = self.clean_spn.read(job.entry, sector_slack=True)
            clean = self.ps2tex.decode(data, self.ps2tex.parse(data)[job.ps2_index])
        except Exception as error:  # noqa: BLE001 - reported, the texture stays as translated
            self.review.append(f"data_usi #{job.entry} t{job.tex}: legend not replaced ({error})")
            return []
        edits = legend(image, base, split, original, clean[:original.shape[0], :original.shape[1]], scale)
        if edits is None:
            self.review.append(f"data_usi #{job.entry} t{job.tex}: legend glyph part differs from the clean "
                               "PS2 picture, left as translated")
            return []
        return edits

    def summary(self):
        from collections import Counter
        glyphs = Counter((g["kind"], g["ps2"], g["x360"]) for r in self.records for g in r["glyphs"])
        memory = Counter(m["removed"] for r in self.records for m in r["memory"])
        return glyphs, memory
