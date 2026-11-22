"""PT-BR for the HD-only message shown when leaving Story mode without saving.

data_usi #2705/#2706 hold messages that exist only on the 360 (storage device, gamer profile, ...): the
PS2 game has no such texts, so the fan translation has none either and they stay in English. One of them is
shown in a red box when the player leaves the Story mode without saving:

  #2706 texture 0  "All unsaved progress will be lost. / End your game?"
               ->  "Todo o progresso sem salvar vai ser perdido. / Encerrar a partida?"
  #2706 texture 3  "Yes / No"  ->  "Sim / Nao"

The new picture is composed from the letters of the same 360 font, cut from the English messages of
#2705/#2706 (white letters with a black outline; nothing is drawn). This font has no accented letters, so
the wording was chosen to need none; "Nao" is written without the tilde (no 360 message has a tilde in
this font, and none is drawn). The line positions (baselines, centre) are the original ones.
Other textures of the entry are kept byte-identical.
"""

from collections import defaultdict

import numpy as np

from . import x360tex

ENTRY = 2706
MESSAGES = {0: ["Todo o progresso sem salvar vai ser perdido.", "Encerrar a partida?"],
            3: ["Sim", "Nao"]}

# English sources of the letters: (entry, texture) -> lines as written in the picture.
SOURCES = {
    (2705, 0): ["A : Continue without saving"],
    (2705, 1): ["B : Select storage device"],
    (2705, 2): ["If you continue playing, the game will", "not Autosave.", "OK?"],
    (2705, 3): ["No save data present."],
    (2705, 7): ["Gamer profile has changed.", "Returned to title screen."],
    (2705, 9): ["Select gamer profile"],
    (2705, 10): ["Continue"],
    (2705, 12): ["The save data is corrupted.", "Please delete damaged data on the Xbox Dashboard."],
    (2705, 17): ["Save failed. Would you like to", "try again? Selecting NO will", "continue without saving."],
    (2706, 0): ["All unsaved progress will be lost.", "End your game?"],
    (2706, 1): ["Are you sure you want to reset to Default?"],
}
ABOVE, BELOW, OUTLINE = 24, 10, 2


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


def _fill(image):
    return (image[..., 3] > 200) & (image[..., :3].min(-1) > 200)


def _lines(fill, count):
    """Row bands of the text lines (i dots merged into their line)."""
    bands = []
    for band in _runs(fill.any(1)):
        if band[1] - band[0] <= 4 and bands and band[0] - bands[-1][1] <= 3:
            bands[-1] = (bands[-1][0], band[1])
        elif bands and band[0] - bands[-1][1] <= 3:
            bands[-1] = (bands[-1][0], band[1])
        else:
            bands.append(band)
    return bands if len(bands) == count else None


def _baseline(fill, band, cols, chars):
    y0, y1 = band
    bottoms = [y0 + np.nonzero(fill[y0:y1, c0:c1].any(1))[0].max()
               for (c0, c1), ch in zip(cols, chars) if ch not in "gjpqy,Q"]
    return int(np.median(bottoms))


def letters(images):
    """{char: (crop, fill x offset in crop, fill width)}, letter gap, word gap; crops are ABOVE+BELOW
    rows high around the baseline, holding only the letter and its outline."""
    found = defaultdict(list)
    gaps, spaces = [], []
    for key, lines in SOURCES.items():
        image = images[key]
        fill = _fill(image)
        bands = _lines(fill, len(lines))
        if bands is None:
            continue
        for band, text in zip(bands, lines):
            y0, y1 = band
            cols = _runs(fill[y0:y1].any(0))
            chars = text.replace(" ", "")
            if len(cols) != len(chars):
                continue
            base = _baseline(fill, band, cols, chars)
            if base - ABOVE < 0 or base + BELOW > image.shape[0]:
                continue
            k = 0
            for word in text.split(" "):
                for j, ch in enumerate(word):
                    c0, c1 = cols[k]
                    if k:
                        (gaps if j else spaces).append(c0 - cols[k - 1][1])
                    own = np.zeros(fill.shape, dtype=bool)
                    own[y0:y1, c0:c1] = fill[y0:y1, c0:c1]
                    for _ in range(OUTLINE):
                        grown = own.copy()
                        grown[1:] |= own[:-1]
                        grown[:-1] |= own[1:]
                        grown[:, 1:] |= own[:, :-1]
                        grown[:, :-1] |= own[:, 1:]
                        own = grown
                    x0 = max(0, c0 - 4)
                    crop = (image * own[..., None])[base - ABOVE:base + BELOW, x0:c1 + 4]
                    found[ch].append((crop, c0 - x0, c1 - c0))
                    k += 1
    glyphs = {ch: items[0] for ch, items in found.items()}
    return glyphs, int(np.median(gaps)), int(np.median(spaces))


def compose(original, lines, glyphs, gap, space):
    """New picture with `lines` on the baselines of the original lines, centred on the same x."""
    fill = _fill(original)
    bands = _lines(fill, len(lines))
    if bands is None:
        raise ValueError("line count differs from the original picture")
    centres, bases = [], []
    for band in bands:
        cols = _runs(fill[band[0]:band[1]].any(0))
        centres.append((cols[0][0] + cols[-1][1]) / 2)
        heights = [band[0] + np.nonzero(fill[band[0]:band[1], c0:c1].any(1))[0].max() for c0, c1 in cols]
        bases.append(int(np.median(heights)))
    # padded so that letters near the top edge (their crop reaches ABOVE rows over the baseline) are not cut
    out = np.zeros((original.shape[0] + ABOVE, original.shape[1] + 8, 4), dtype=np.uint8)
    for text, base, centre in zip(lines, bases, centres):
        parts = [(glyphs[ch], j == 0) for word in text.split(" ") for j, ch in enumerate(word)]
        missing = [ch for ch in text.replace(" ", "") if ch not in glyphs]
        if missing:
            raise ValueError(f"letters missing from the 360 font: {missing}")
        total = sum(g[2] for g, _ in parts) + sum(space if first else gap for _, first in parts[1:])
        x = int(round(centre - total / 2))
        for i, ((crop, offset, width), first) in enumerate(parts):
            if i:
                x += space if first else gap
            X = x - offset + 4
            region = out[base:base + ABOVE + BELOW, X:X + crop.shape[1]]
            # white fill over black outline: the per-pixel maximum keeps each fill over its neighbours' outlines
            region[...] = np.maximum(region, crop[:region.shape[0], :region.shape[1]])
            x += width
    return out[ABOVE:, 4:4 + original.shape[1]]


def build(usi, decompress):
    """(new #AZT bytes of data_usi #2706, [(texture, English picture, new picture)])."""
    images = {}
    for entry in {e for e, _ in SOURCES}:
        azt = x360tex.parse(decompress(usi.read(entry)))
        for k, t in enumerate(azt.textures):
            images[(entry, k)] = x360tex.decode(t)[:t.height, :t.width]
    glyphs, gap, space = letters(images)
    azt = x360tex.parse(decompress(usi.read(ENTRY)))
    untouched = {k: t for k, t in enumerate(azt.textures) if k not in MESSAGES}
    done = []
    for index, lines in MESSAGES.items():
        tex = azt.textures[index]
        original = images[(ENTRY, index)]
        picture = compose(original, lines, glyphs, gap, space)
        dw, dh = tex.dds_dimensions()
        canvas = np.zeros((dh, dw, 4), dtype=np.uint8)
        canvas[:picture.shape[0], :picture.shape[1]] = picture
        azt.textures[index] = x360tex.Texture(tex.words, x360tex.encode_like(tex, canvas))
        done.append((index, original, picture))
    data = x360tex.serialize(azt)
    again = x360tex.parse(data)
    for k, t in untouched.items():
        if t is None or again.textures[k].words != t.words or again.textures[k].dds != t.dds:
            raise ValueError(f"texture {k} of #{ENTRY} changed but should stay byte-identical")
    return data, done
