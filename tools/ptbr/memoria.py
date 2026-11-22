"""Memory card messages: drop the "PS2" the translation kept from the PS2 game.

The PT-BR messages say "memory card (PS2)" and "nem reinicie o PS2" (load/save screens #1995/#2025, the
save/load captions of #1999, the E.S.C. notice on the help pages #1980). On the Switch there is no PS2,
so " (PS2)" and " o PS2" are cut out of the picture and the rest of the line is closed up (and centred
again when the line was centred). Replacing the words with "memória do Switch" would need letters the
translated pictures do not have in the same font (there is no "w" in these fonts), so the PS2 part is
only removed.

Finding the words, per picture:
  "(PS2)"  a pair of parentheses (tall, narrow, curved strokes) on the same line whose inside is about
           three letters wide (wider pairs, e.g. "(Prata)", are left alone);
  "o PS2"  the inside of a "(PS2)" of the same container (template()) matched elsewhere on a line,
           preceded by the one-letter word "o"; the cut removes " o PS2" and keeps what follows.
"""

import numpy as np

ENTRIES = {1995: None, 2025: None, 1999: (18, 19), 1980: (3,)}   # entry -> textures (None = all)


def _runs(values):
    out, start = [], None
    for i, v in enumerate(values):
        if v and start is None:
            start = i
        if not v and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, len(values)))
    return out


def _label(mask):
    from .botoes import _label as label
    return label(mask)


class Page:
    """Ink mask and background model of a text picture."""

    def __init__(self, image):
        self.image = image
        rgb = image[..., :3].astype(int)
        a = image[..., 3]
        self.opaque_bg = (a > 150).mean() > 0.9
        if self.opaque_bg:
            bg = np.median(rgb[a > 150], axis=0)
            self.ink = (a > 150) & (np.abs(rgb - bg).max(-1) > 60)
        else:
            self.ink = a > 100
        # strokes used to find the parentheses: the white fill of the outlined menu font, else the ink
        light = (a > 150) & (rgb.min(-1) > 180)
        if self.opaque_bg:
            self.stroke = (a > 150) & (rgb.mean(-1) < 110)   # dark letters, light outline, on a picture
        elif light.sum() > 0.1 * self.ink.sum():
            self.stroke = light
        else:
            self.stroke = self.ink

    def lines(self):
        return _runs(self.ink.any(1))

    def words(self, y0, y1, gap):
        cols = _runs(self.ink[y0:y1].any(0))
        words = []
        for x0, x1 in cols:
            if words and x0 - words[-1][1] < gap:
                words[-1] = (words[-1][0], x1)
            else:
                words.append((x0, x1))
        return words


def _paren(stroke, comp):
    """'(' / ')' / None for a stroke component (y0, x0, y1, x1, n)."""
    y0, x0, y1, x1, n = comp
    h, w = y1 - y0, x1 - x0
    if h < 10 or w > max(6, h * 0.4) or w < 2:
        return None
    sub = stroke[y0:y1, x0:x1]
    rows = [np.nonzero(r)[0] for r in sub]
    xs = [r.mean() for r in rows if len(r)]
    if len(xs) < h * 0.8:
        return None
    q = max(1, len(xs) // 4)
    ends = (np.mean(xs[:q]) + np.mean(xs[-q:])) / 2
    middle = np.mean(xs[len(xs) // 2 - q // 2:len(xs) // 2 + q // 2 + 1])
    if ends - middle > 0.8:
        return "("
    if middle - ends > 0.8:
        return ")"
    return None


def find_ps2(page):
    """[(x0, x1, y0, y1, 'paren'|'bare')] spans to delete, plus the line band they belong to."""
    comps = [c for c in _label(page.stroke) if c[4] >= 6]
    parens = [(c, _paren(page.stroke, c)) for c in comps]
    lefts = [c for c, p in parens if p == "("]
    rights = [c for c, p in parens if p == ")"]
    spans = []
    for l in lefts:
        best = None
        for r in rights:
            if r[1] <= l[3] or abs(r[0] - l[0]) > 3 or abs(r[2] - l[2]) > 3:
                continue
            inner = r[1] - l[3]
            h = l[2] - l[0]
            if 1.2 * h <= inner <= 2.6 * h and (best is None or r[1] < best[1]):
                best = r
        if best is not None:
            spans.append((l[1], best[3], min(l[0], best[0]), max(l[2], best[2]), "paren"))
    return spans


def template(page, spans=None):
    """Tight stroke mask of the "PS2" inside the first "(PS2)" of a picture, or None."""
    spans = find_ps2(page) if spans is None else spans
    if not spans:
        return None
    x0, x1, y0, y1, _ = spans[0]
    stroke = page.stroke
    comps = sorted((c for c in _label(stroke[y0:y1, x0:x1]) if c[4] >= 3), key=lambda c: c[1])
    if len(comps) < 3:
        return None
    sub = stroke[y0:y1, x0 + comps[0][3]:x0 + comps[-1][1]]
    ys, xs = np.nonzero(sub)
    return sub[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def _bare(page, spans, templates):
    """'o PS2' without parentheses: a "PS2" (as written inside the parentheses of the same container)
    found outside every "(PS2)"."""
    stroke = page.stroke
    H, W = stroke.shape
    found = []
    for tmpl in templates:
        th, tw = tmpl.shape
        if th > H or tw > W:
            continue
        win = np.lib.stride_tricks.sliding_window_view(stroke, (th, tw)).astype(np.float32)
        t = tmpl.astype(np.float32)
        inter = np.einsum("ijkl,kl->ij", win, t)
        iou = inter / np.maximum(win.sum(axis=(2, 3)) + t.sum() - inter, 1)
        for y, x in sorted(zip(*np.nonzero(iou > 0.6)), key=lambda p: -iou[p]):
            if any(abs(y - fy) < th and abs(x - fx) < fw for fy, fx, fw, fh in found):
                continue
            if any(sx0 - 2 <= x <= sx1 and sy0 - 2 <= y <= sy1 for sx0, sx1, sy0, sy1, _ in spans):
                continue
            found.append((int(y), int(x), tw, th))
    return [(x, x + tw, y, y + th, "bare") for y, x, tw, th in found]


def _band(page, y0, y1, x0, x1):
    """Rows of the text line holding the span: the line's fill rows (near the span), extended halfway
    to the neighbouring lines (outlined fonts touch the next line through the outline)."""
    rows = _runs(page.stroke[:, max(0, x0 - 40):x1 + 40].any(1))
    H = page.ink.shape[0]
    for i, (a, b) in enumerate(rows):
        if a <= y1 - 1 and y0 <= b - 1:
            top = 0 if i == 0 else (rows[i - 1][1] + a) // 2
            bottom = H if i == len(rows) - 1 else (b + rows[i + 1][0] + 1) // 2
            ink_rows = np.nonzero(page.ink[top:bottom, max(0, x0 - 40):x1 + 40].any(1))[0]
            return top + int(ink_rows.min()), top + int(ink_rows.max()) + 1
    return max(0, y0 - 4), min(H, y1 + 4)


def _fill(image, y0, y1, x0, x1, opaque_bg):
    if x1 <= x0:
        return
    if not opaque_bg:
        image[y0:y1, x0:x1] = 0
        return
    # backdrop just outside the line, past any outline
    x0, x1 = max(1, x0 - 3), min(image.shape[1] - 1, x1 + 3)
    left = image[y0:y1, x0 - 1].astype(np.float32)
    right = image[y0:y1, x1].astype(np.float32)
    t = np.linspace(0, 1, x1 - x0, dtype=np.float32)[None, :, None]
    image[y0:y1, x0:x1] = np.rint(left[:, None] * (1 - t) + right[:, None] * t).astype(np.uint8)


def edit(image, templates=()):
    """Removes ' (PS2)' and ' o PS2' from a text picture in place. Returns the list of edits
    [(kind, (x0, y0, x1, y1) of the removed span, line band)]."""
    page = Page(image)
    spans = find_ps2(page)
    own = template(page, spans)
    spans += _bare(page, spans, ([own] if own is not None else []) + list(templates))
    edits = []
    by_line = {}
    for s in spans:
        band = _band(page, s[2], s[3], s[0], s[1])
        by_line.setdefault(band, []).append(s)
    for (by0, by1), items in by_line.items():
        # glyphs of the line as stroke components (the fill, for outlined fonts); the cut starts right
        # after the outline of the glyph before the removed words and ends after the token's outline
        pad = 2 if page.stroke is not page.ink else 0
        glyphs = sorted((c for c in _label(page.stroke[by0:by1]) if c[4] >= 3), key=lambda c: c[1])
        # the line: glyphs chained from the first span with gaps below a line height (skips art)
        h = by1 - by0
        seed = min(items)[0]
        line = [c for c in glyphs if c[1] <= seed + 1 and c[3] >= seed - 1] or                [min(glyphs, key=lambda c: abs(c[1] - seed))]
        lo, hi = min(c[1] for c in line), max(c[3] for c in line)
        for c in sorted(glyphs, key=lambda c: -c[3]):
            if c[3] <= lo and lo - c[3] <= h:
                lo = c[1]
        for c in glyphs:
            if c[1] >= hi and c[1] - hi <= h:
                hi = c[3]
        glyphs = [c for c in glyphs if c[1] >= lo and c[3] <= hi]
        line_x0, line_x1 = max(0, lo - pad), min(image.shape[1], hi + pad)
        # take the whole outline at both ends (it can be wider than `pad`)
        ink_cols = page.ink[by0:by1].any(0)
        while line_x0 > 0 and ink_cols[line_x0 - 1] and lo - line_x0 < 6:
            line_x0 -= 1
        while line_x1 < image.shape[1] and ink_cols[line_x1] and line_x1 - hi < 6:
            line_x1 += 1
        centred = abs(line_x0 - (image.shape[1] - line_x1)) <= max(4, 0.03 * image.shape[1])
        band = image[by0:by1].copy()
        cuts = []
        for x0, x1, y0, y1, kind in sorted(items):
            before = [c for c in glyphs if c[3] <= x0]
            if not before:
                continue
            last = before[-1]
            if kind == "bare":
                # "<word> o PS2": the glyph before the token must be a lone "o" after a word gap
                gap_o = x0 - last[3]
                if len(before) < 2 or (last[3] - last[1]) > 0.8 * (by1 - by0) or last[1] - before[-2][3] < gap_o - 1:
                    continue
                start = before[-2][3] + pad
            else:
                start = last[3] + pad
            cuts.append((start, x1 + pad, kind, (x0, y0, x1, y1)))
        if not cuts:
            continue
        keep = []
        position = line_x0
        for start, end, kind, box in cuts:
            keep.append((position, start))
            position = end
            edits.append((kind, box, (by0, by1)))
        keep.append((position, line_x1))
        pieces = [band[:, a:b] for a, b in keep if b > a]
        text = np.concatenate(pieces, axis=1)
        _fill(image, by0, by1, line_x0, line_x1, page.opaque_bg)
        new_x0 = line_x0
        if centred:
            new_x0 = (image.shape[1] - text.shape[1]) // 2
        image[by0:by1, new_x0:new_x0 + text.shape[1]] = text
    return edits
