"""DXT3 (BC2) encoder and decoder in numpy, little-endian block layout (the 360 swap is done by the caller).

Block = 8 bytes explicit 4-bit alpha (row-major, low nibble first) + colour block (u16 c0, u16 c1,
u32 2-bit indices). In DXT3 the colour block is always the 4-colour mode, whatever the order of c0/c1.
The encoder fits the colours of the non-transparent pixels of each block along the axis of their
bounding box, which is near exact for the dialogue images (anti-aliased black text with a white
outline). Fully transparent blocks are written as zero alpha and black.
"""

import numpy as np


def _to_blocks(rgba):
    h, w, _ = rgba.shape
    return rgba.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)


def _from_blocks(blocks, w, h):
    return blocks.reshape(h // 4, w // 4, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, 4)


def _expand565(c):
    c = c.astype(np.int32)
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1)


def _pack565(rgb):
    rgb = np.clip(np.rint(rgb), 0, 255).astype(np.int32)
    return ((rgb[..., 0] * 31 + 127) // 255 << 11) | ((rgb[..., 1] * 63 + 127) // 255 << 5) | \
        ((rgb[..., 2] * 31 + 127) // 255)


def _palette(c0, c1):
    p0, p1 = _expand565(c0), _expand565(c1)
    return np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], axis=1)  # (N, 4, 3)


_MIX = np.array([[1.0, 0.0], [0.0, 1.0], [2 / 3, 1 / 3], [1 / 3, 2 / 3]])  # palette slot = a*c0 + b*c1


def _fit(rgb, weight, e0, e1):
    """Quantizes endpoints, keeps c0 >= c1 (4-colour order), picks nearest indices, returns weighted error."""
    c0, c1 = _pack565(e0), _pack565(e1)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    pal = _palette(c0, c1).astype(np.float64)  # (n, 4, 3)
    dist = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(axis=3)  # (n, 16, 4)
    index = dist.argmin(axis=2)
    error = (dist.min(axis=2) * weight ** 2).sum(axis=1)
    return c0, c1, index, error


def _refine(rgb, weight, index, c0, c1):
    """Weighted least-squares endpoints for fixed indices (falls back to the current ones if singular)."""
    mix = _MIX[index]  # (n, 16, 2)
    w = weight[..., None]
    ata = np.einsum("nki,nkj->nij", mix * w, mix)
    atb = np.einsum("nki,nkc->nic", mix * w, rgb)
    det = ata[:, 0, 0] * ata[:, 1, 1] - ata[:, 0, 1] * ata[:, 1, 0]
    ok = np.abs(det) > 1e-9
    safe = np.where(ok, det, 1.0)
    inv = np.stack([np.stack([ata[:, 1, 1], -ata[:, 0, 1]], -1), np.stack([-ata[:, 1, 0], ata[:, 0, 0]], -1)], 1)
    sol = np.einsum("nij,njc->nic", inv, atb) / safe[:, None, None]
    current = np.stack([_expand565(c0), _expand565(c1)], 1).astype(np.float64)
    sol = np.where(ok[:, None, None], sol, current)
    return sol[:, 0], sol[:, 1]


def decode_dxt3(data, width, height):
    blocks = np.frombuffer(data[:width * height], dtype=np.uint8).reshape(-1, 16)
    alpha_bytes = blocks[:, :8]
    alpha = np.empty((len(blocks), 16), dtype=np.uint8)
    alpha[:, 0::2] = alpha_bytes & 15
    alpha[:, 1::2] = alpha_bytes >> 4
    alpha = alpha * 17
    c0 = blocks[:, 8].astype(np.int32) | (blocks[:, 9].astype(np.int32) << 8)
    c1 = blocks[:, 10].astype(np.int32) | (blocks[:, 11].astype(np.int32) << 8)
    bits = blocks[:, 12:16].astype(np.uint32)
    bits = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    index = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3
    colour = np.take_along_axis(_palette(c0, c1), index[..., None].astype(np.int64), axis=1)
    out = np.concatenate([colour.astype(np.uint8), alpha[..., None]], axis=2)
    return _from_blocks(out, width, height)


def encode_dxt3(rgba):
    h, w, _ = rgba.shape
    blocks = _to_blocks(rgba.astype(np.float64))
    n = len(blocks)
    rgb, a = blocks[..., :3], blocks[..., 3]
    # alpha: 4 bits, rounded
    a4 = np.clip(np.rint(a / 17.0), 0, 15).astype(np.uint8)
    alpha_bytes = a4[:, 0::2] | (a4[:, 1::2] << 4)
    # colour endpoints from the visible pixels only
    visible = a4 > 0
    any_visible = visible.any(axis=1)
    lo = np.where(visible[..., None], rgb, np.inf).min(axis=1)
    hi = np.where(visible[..., None], rgb, -np.inf).max(axis=1)
    lo = np.where(any_visible[:, None], lo, 0.0)
    hi = np.where(any_visible[:, None], hi, 0.0)
    axis = hi - lo
    proj = np.einsum("nkc,nc->nk", rgb, axis)
    kmin = np.where(visible, proj, np.inf).argmin(axis=1)
    kmax = np.where(visible, proj, -np.inf).argmax(axis=1)
    e0 = rgb[np.arange(n), kmax]
    e1 = rgb[np.arange(n), kmin]
    e0 = np.where(any_visible[:, None], e0, 0.0)
    e1 = np.where(any_visible[:, None], e1, 0.0)
    # colour error is weighted by alpha: what matters is the blended (premultiplied) result
    weight = np.where(visible, a, 0.0) / 255.0
    c0, c1, index, error = _fit(rgb, weight, e0, e1)
    # one least-squares refinement of the endpoints from the chosen indices; kept per block if better
    r0, r1 = _refine(rgb, weight, index, c0, c1)
    d0, d1, dindex, derror = _fit(rgb, weight, r0, r1)
    better = derror < error
    c0, c1 = np.where(better, d0, c0), np.where(better, d1, c1)
    index = np.where(better[:, None], dindex, index)
    index = np.where(visible, index, 0).astype(np.uint32)  # invisible pixels: any colour
    bits = (index << (2 * np.arange(16, dtype=np.uint32))[None, :]).sum(axis=1, dtype=np.uint64).astype(np.uint32)
    out = np.empty((n, 16), dtype=np.uint8)
    out[:, :8] = alpha_bytes
    out[:, 8], out[:, 9] = c0 & 255, (c0 >> 8) & 255
    out[:, 10], out[:, 11] = c1 & 255, (c1 >> 8) & 255
    for k in range(4):
        out[:, 12 + k] = (bits >> (8 * k)) & 255
    return out.tobytes()


def _colour_block(blocks, punch_through):
    c0 = blocks[:, 0].astype(np.int32) | (blocks[:, 1].astype(np.int32) << 8)
    c1 = blocks[:, 2].astype(np.int32) | (blocks[:, 3].astype(np.int32) << 8)
    bits = blocks[:, 4:8].astype(np.uint32)
    bits = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    index = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3
    palette = _palette(c0, c1)
    alpha = np.full((len(blocks), 4), 255, dtype=np.uint8)
    if punch_through:  # DXT1 3-colour mode when c0 <= c1
        three = c0 <= c1
        p0, p1 = _expand565(c0), _expand565(c1)
        palette[three, 2] = (p0[three] + p1[three]) // 2
        palette[three, 3] = 0
        alpha[three, 3] = 0
    colour = np.take_along_axis(palette, index[..., None].astype(np.int64), axis=1)
    a = np.take_along_axis(alpha, index.astype(np.int64), axis=1)
    return colour.astype(np.uint8), a


def decode_dxt1(data, width, height):
    """Used only to look at 360 textures (matching checks, contact sheets)."""
    blocks = np.frombuffer(data[:width * height // 2], dtype=np.uint8).reshape(-1, 8)
    colour, alpha = _colour_block(blocks, True)
    return _from_blocks(np.concatenate([colour, alpha[..., None]], axis=2), width, height)


def decode_dxt5(data, width, height):
    """Used only to look at 360 textures (matching checks, contact sheets)."""
    blocks = np.frombuffer(data[:width * height], dtype=np.uint8).reshape(-1, 16)
    a0, a1 = blocks[:, 0].astype(np.int32), blocks[:, 1].astype(np.int32)
    bits = np.zeros(len(blocks), dtype=np.uint64)
    for k in range(6):
        bits |= blocks[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    index = ((bits[:, None] >> (3 * np.arange(16, dtype=np.uint64))[None, :]) & np.uint64(7)).astype(np.int64)
    table = np.zeros((len(blocks), 8), dtype=np.int32)
    table[:, 0], table[:, 1] = a0, a1
    eight = a0 > a1
    for k in range(1, 7):
        table[eight, k + 1] = ((7 - k) * a0[eight] + k * a1[eight]) // 7
    for k in range(1, 5):
        table[~eight, k + 1] = ((5 - k) * a0[~eight] + k * a1[~eight]) // 5
    table[~eight, 6], table[~eight, 7] = 0, 255
    alpha = np.take_along_axis(table, index, axis=1).astype(np.uint8)
    colour, _ = _colour_block(blocks[:, 8:], False)
    return _from_blocks(np.concatenate([colour, alpha[..., None]], axis=2), width, height)


def encode_dxt1(rgba):
    """DXT1 (BC1) from the DXT3 colour fit. Opaque blocks use the 4-colour mode (c0 > c1); blocks
    with pixels below alpha 128 use the 3-colour + transparent mode (c0 <= c1)."""
    binary = rgba.copy()
    opaque_px = rgba[..., 3] >= 128
    binary[..., 3] = np.where(opaque_px, 255, 0)
    blocks = np.frombuffer(encode_dxt3(binary), dtype=np.uint8).reshape(-1, 16)[:, 8:].copy()
    transparent = ~_to_blocks(np.repeat(opaque_px[..., None], 4, axis=2).astype(np.uint8))[..., 0].astype(bool)
    has_holes = transparent.any(axis=1)
    c0 = blocks[:, 0].astype(np.int32) | (blocks[:, 1].astype(np.int32) << 8)
    c1 = blocks[:, 2].astype(np.int32) | (blocks[:, 3].astype(np.int32) << 8)
    bits = blocks[:, 4:8].astype(np.uint32)
    bits = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    index = ((bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3).astype(np.uint32)
    swap = np.where(has_holes, c0 > c1, c0 < c1)
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    index = np.where(swap[:, None], index ^ 1, index)   # 0<->1, 2<->3
    equal = (c0 == c1) & ~has_holes
    index = np.where(equal[:, None], 0, index)
    holes = has_holes[:, None]
    index = np.where(holes & (index >= 2), 2, index)     # the two thirds -> midpoint
    index = np.where(holes & transparent, 3, index)
    bits = (index << (2 * np.arange(16, dtype=np.uint32))[None, :]).sum(axis=1, dtype=np.uint64).astype(np.uint32)
    out = np.empty((len(blocks), 8), dtype=np.uint8)
    out[:, 0], out[:, 1] = c0 & 255, (c0 >> 8) & 255
    out[:, 2], out[:, 3] = c1 & 255, (c1 >> 8) & 255
    for k in range(4):
        out[:, 4 + k] = (bits >> (8 * k)) & 255
    return out.tobytes()


def encode_dxt5(rgba):
    """DXT5 (BC3): interpolated alpha (8-value mode between the block's max and min) + the DXT3
    colour block (BC3 colour blocks are always in the 4-colour mode, like DXT3)."""
    colour = np.frombuffer(encode_dxt3(rgba), dtype=np.uint8).reshape(-1, 16)[:, 8:]
    a = _to_blocks(rgba.astype(np.float64))[..., 3]
    a0, a1 = a.max(axis=1).astype(np.int32), a.min(axis=1).astype(np.int32)
    table = np.zeros((len(a), 8), dtype=np.float64)
    table[:, 0], table[:, 1] = a0, a1
    for k in range(1, 7):
        table[:, k + 1] = ((7 - k) * a0 + k * a1) // 7
    index = np.abs(a[:, :, None] - table[:, None, :]).argmin(axis=2).astype(np.uint64)
    index = np.where((a0 == a1)[:, None], 0, index).astype(np.uint64)
    bits = (index << (3 * np.arange(16, dtype=np.uint64))[None, :]).sum(axis=1, dtype=np.uint64)
    out = np.empty((len(a), 16), dtype=np.uint8)
    out[:, 0], out[:, 1] = a0, a1
    for k in range(6):
        out[:, 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(255)).astype(np.uint8)
    out[:, 8:] = colour
    return out.tobytes()
