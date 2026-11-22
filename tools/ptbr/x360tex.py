"""Xbox 360 "#AZT" texture containers (the HD Collection's replacement for the PS2 "#AMT").

All fields big-endian:
  0x00 "#AZT", 0x04..0x0F zero, 0x10 u32 texture count, 0x14 u32 offset of the offset table (0x20),
  0x18, 0x1C u32 per-entry values (kept verbatim from the 360 entry used as template),
  0x20 table: count x u32 offset of each 0x30-byte texture header (0 = empty slot).
Texture header (u32 words unless noted):
  +0x00 0, +0x04 id (0x21), +0x08 u16 width, u16 height, +0x0C u16 log2 width, u16 log2 height,
  +0x10 u16 width, u16 height (again), +0x14 offset of the DDS, +0x18 size of the DDS,
  +0x1C..+0x2F kept verbatim (they mirror PS2 fields: flags, palette offset, palette size).
Each DDS is a plain PC DDS: little-endian header (0x80 bytes) followed by pixel data covering the
power-of-two size (1 << log2), linear (not tiled) and NOT byte-swapped (checked: the alpha of the
unswapped DXT3 data matches the PS2 source with correlation 0.9999, swapped 16-bit only 0.31).
Formats seen in data_usi.afs: DXT3 (dialogue and most images), uncompressed 32-bit (menus),
uncompressed 16-bit A4R4G4B4 (masks A 0xF000, R 0x0F00, G 0x00F0, B 0x000F, little-endian u16, a few
menu textures), a few DXT1/DXT5.
Word +0x0C (log2 size) holds the log2 of the DDS size for most textures; textures that the 360
upscaled keep the PS2 log2 values there. The game takes the texture size from this header (the same
index has different sizes in data_usi.afs and data_spn.afs, read by the same code), so a texture
may change size as long as the header and the DDS agree.
"""

import struct

import numpy as np

AZT_MAGIC = b"#AZT"
DDS_HEADER = 0x80


class AztError(Exception):
    pass


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


class Texture:
    def __init__(self, words, dds):
        self.words = list(words)  # the twelve u32 of the texture header
        self.dds = bytes(dds)

    @property
    def width(self):
        return self.words[2] >> 16

    @property
    def height(self):
        return self.words[2] & 0xFFFF

    @property
    def pot_size(self):
        return 1 << (self.words[3] >> 16), 1 << (self.words[3] & 0xFFFF)

    @property
    def fourcc(self):
        return self.dds[0x54:0x58]

    def dds_dimensions(self):
        height, width = struct.unpack_from("<II", self.dds, 0x0C)
        return width, height


class Azt:
    def __init__(self, header, textures):
        self.header = bytes(header)  # first 0x20 bytes, count/table fields rewritten on output
        self.textures = textures

    def dims(self):
        return [(t.width, t.height) if t else None for t in self.textures]


def parse(data):
    if data[:4] != AZT_MAGIC:
        raise AztError("not an #AZT container")
    count, table = struct.unpack_from(">II", data, 0x10)
    offsets = struct.unpack_from(">%dI" % count, data, table)
    textures = []
    for offset in offsets:
        if offset == 0:  # empty slot: the table can be sparse
            textures.append(None)
            continue
        words = struct.unpack_from(">12I", data, offset)
        dds_offset, dds_size = words[5], words[6]
        dds = data[dds_offset:dds_offset + dds_size]
        if dds[:4] != b"DDS ":
            raise AztError(f"texture at {offset:#x} has no DDS header")
        textures.append(Texture(words, dds))
    return Azt(data[:0x20], textures)


def serialize(azt):
    """Inverse of parse(): rebuilds the offsets; byte-identical for the game's own containers."""
    count = len(azt.textures)
    present = [t for t in azt.textures if t is not None]
    table = 0x20
    first_header = table + 4 * count
    position = _align(first_header + 0x30 * len(present), 0x20)
    headers, blobs, offsets = [], [], []
    for texture in azt.textures:
        if texture is None:
            offsets.append(0)
            continue
        offsets.append(first_header + 0x30 * len(headers))
        words = list(texture.words)
        words[5], words[6] = position, len(texture.dds)
        headers.append(struct.pack(">12I", *words))
        blobs.append((position, texture.dds))
        position = _align(position + len(texture.dds), 0x20)
    out = bytearray(position)
    out[:0x20] = azt.header
    struct.pack_into(">II", out, 0x10, count, table)
    struct.pack_into(">%dI" % count, out, table, *offsets)
    for i, header in enumerate(headers):
        out[first_header + 0x30 * i:first_header + 0x30 * (i + 1)] = header
    for offset, blob in blobs:
        out[offset:offset + len(blob)] = blob
    return bytes(out)


# ---------------------------------------------------------------------------------------------------
# Pixel data
# ---------------------------------------------------------------------------------------------------

def _rgba32_masks(texture):
    """Channel byte positions of an uncompressed 32-bit DDS, from its pixel-format masks."""
    flags, fourcc, bits, r, g, b, a = struct.unpack_from("<I4sIIIII", texture.dds, 0x50)
    if fourcc != bytes(4) or bits != 32:
        return None
    position = {0xFF: 0, 0xFF00: 1, 0xFF0000: 2, 0xFF000000: 3}
    try:
        return [position[r], position[g], position[b], position[a] if a else None]
    except KeyError:
        raise AztError("unsupported 32-bit channel masks")


def _argb4444(texture):
    flags, fourcc, bits, r, g, b, a = struct.unpack_from("<I4sIIIII", texture.dds, 0x50)
    return fourcc == bytes(4) and bits == 16 and (r, g, b, a) == (0x0F00, 0x00F0, 0x000F, 0xF000)


def decode(texture):
    """RGBA uint8 array (pot_height, pot_width, 4) of a 360 texture."""
    width, height = texture.dds_dimensions()
    body = texture.dds[DDS_HEADER:]
    if texture.fourcc == b"DXT3":
        from .dxt3 import decode_dxt3
        return decode_dxt3(body, width, height)
    if texture.fourcc == b"DXT1":
        from .dxt3 import decode_dxt1
        return decode_dxt1(body, width, height)
    if texture.fourcc == b"DXT5":
        from .dxt3 import decode_dxt5
        return decode_dxt5(body, width, height)
    if _argb4444(texture):
        raw = np.frombuffer(body[:width * height * 2], dtype="<u2").reshape(height, width).astype(np.uint16)
        out = np.stack([(raw >> 8) & 15, (raw >> 4) & 15, raw & 15, raw >> 12], axis=-1).astype(np.uint8)
        return out * 17
    masks = _rgba32_masks(texture)
    if masks:
        raw = np.frombuffer(body[:width * height * 4], dtype=np.uint8).reshape(height, width, 4)
        out = np.empty_like(raw)
        for channel, position in enumerate(masks):
            out[..., channel] = raw[..., position] if position is not None else 255
        return out
    raise AztError(f"unsupported 360 texture format {texture.fourcc!r}")


def encode_like(texture, rgba):
    """New DDS bytes for `texture` (same header and format) from an RGBA image of the POT size."""
    width, height = texture.dds_dimensions()
    if rgba.shape[:2] != (height, width):
        raise AztError("image size does not match the DDS")
    header = texture.dds[:DDS_HEADER]
    if texture.fourcc == b"DXT3":
        from .dxt3 import encode_dxt3
        body = encode_dxt3(rgba)
    elif texture.fourcc == b"DXT1":
        from .dxt3 import encode_dxt1
        body = encode_dxt1(rgba)
    elif texture.fourcc == b"DXT5":
        from .dxt3 import encode_dxt5
        body = encode_dxt5(rgba)
    elif _argb4444(texture):
        q = (rgba.astype(np.uint16) * 15 + 127) // 255
        raw = (q[..., 3] << 12) | (q[..., 0] << 8) | (q[..., 1] << 4) | q[..., 2]
        body = raw.astype("<u2").tobytes()
    else:
        masks = _rgba32_masks(texture)
        if not masks:
            flags, bits = struct.unpack_from("<I", texture.dds, 0x50)[0], struct.unpack_from("<I", texture.dds, 0x58)[0]
            raise AztError(f"unsupported 360 texture format (fourcc {texture.fourcc!r}, "
                           f"pixel flags {flags:#x}, {bits} bpp)")
        raw = np.zeros((height, width, 4), dtype=np.uint8)
        for channel, position in enumerate(masks):
            if position is not None:
                raw[..., position] = rgba[..., channel]
        body = raw.tobytes()
    if len(header) + len(body) != len(texture.dds):
        raise AztError("encoded size differs from the template DDS")
    return header + body


def _next_pow2(value):
    return 1 << max(0, (value - 1).bit_length())


def bytes_per_dds(texture, width, height):
    flags, fourcc, bits = struct.unpack_from("<I4sI", texture.dds, 0x50)
    if fourcc in (b"DXT2", b"DXT3", b"DXT4", b"DXT5"):
        return max(1, width // 4) * max(1, height // 4) * 16
    if fourcc == b"DXT1":
        return max(1, width // 4) * max(1, height // 4) * 8
    if fourcc == bytes(4) and bits in (16, 32):
        return width * height * bits // 8
    raise AztError(f"unsupported 360 texture format {fourcc!r}")


def resized(texture, width, height, ps2_log2=None):
    """Blank texture like `texture` (same format and header words) for an image of width x height.
    The DDS keeps the template's size when the image fits in it, otherwise the next powers of two.
    Word +0x0C follows the template's convention: log2 of the DDS size, or (for upscaled textures
    that kept the PS2 value) `ps2_log2` = (log2 width, log2 height) of the translated PS2 texture."""
    old_w, old_h = texture.dds_dimensions()
    if width <= old_w and height <= old_h:
        dds_w, dds_h = old_w, old_h
    else:
        dds_w, dds_h = max(4, _next_pow2(width)), max(4, _next_pow2(height))
    words = list(texture.words)
    log2_of_dds = words[3] == ((old_w.bit_length() - 1) << 16 | (old_h.bit_length() - 1))
    if log2_of_dds:
        words[3] = (dds_w.bit_length() - 1) << 16 | (dds_h.bit_length() - 1)
    elif ps2_log2 is not None:
        words[3] = ps2_log2[0] << 16 | ps2_log2[1]
    words[2] = words[4] = width << 16 | height
    header = bytearray(texture.dds[:DDS_HEADER])
    flags, fourcc, bits = struct.unpack_from("<I4sI", header, 0x50)
    old_pitch = struct.unpack_from("<I", header, 0x14)[0]
    if fourcc == bytes(4):
        pitch = dds_w * bits // 8
        expected_old = old_w * bits // 8
    else:
        pitch = bytes_per_dds(texture, dds_w, dds_h)
        expected_old = bytes_per_dds(texture, old_w, old_h)
    if old_pitch != expected_old:
        raise AztError("template DDS pitch/linear size has an unknown convention")
    struct.pack_into("<III", header, 0x0C, dds_h, dds_w, pitch)
    body = bytes(bytes_per_dds(texture, dds_w, dds_h))
    return Texture(words, bytes(header) + body)
