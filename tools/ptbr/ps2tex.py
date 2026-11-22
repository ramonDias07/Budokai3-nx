"""PS2 "#AMT" texture containers (Budokai 3 PS2), all fields little-endian.

  0x00 "#AMT", 0x10 u32 texture count, 0x14 u32 offset of the offset table.
Texture header (0x30 bytes, u32 words): +0x04 id, +0x08 GS pixel format (0x14 = PSMT4, 0x13 = PSMT8),
  +0x0C u16 log2 width, u16 log2 height (GS buffer size), +0x10 u16 width, u16 height,
  +0x14 pixel block offset, +0x18 pixel bytes, +0x24 palette block offset, +0x28 palette bytes.
Pixel and palette blocks start with a 0x20-byte GS upload header (GIF tags) before the data.
Pixels are linear, `width` per row, 4-bit indices low nibble first. Palettes are RGBA8888 with the
PS2 alpha range 0..0x80; 256-colour palettes use the GS CSM1 order (entries 8..15 and 16..23 of
every 32 swapped).
"""

import struct

import numpy as np

AMT_MAGIC = b"#AMT"
GS_UPLOAD_HEADER = 0x20
PSMT8, PSMT4 = 0x13, 0x14


class AmtError(Exception):
    pass


class Texture:
    def __init__(self, data, offset):
        words = struct.unpack_from("<12I", data, offset)
        self.id, self.format = words[1], words[2]
        self.log2_width, self.log2_height = struct.unpack_from("<HH", data, offset + 0x0C)
        self.width, self.height = struct.unpack_from("<HH", data, offset + 0x10)
        self.pixel_offset, self.pixel_size = words[5], words[6]
        self.palette_offset, self.palette_size = words[9], words[10]

    @property
    def pot_size(self):
        return 1 << self.log2_width, 1 << self.log2_height


def parse(data):
    if data[:4] != AMT_MAGIC:
        raise AmtError("not an #AMT container")
    count, table = struct.unpack_from("<II", data, 0x10)
    return [Texture(data, o) for o in struct.unpack_from("<%dI" % count, data, table)]


def _csm1(palette):
    index = np.arange(256)
    swapped = (index & 0xE7) | ((index & 0x08) << 1) | ((index & 0x10) >> 1)
    return palette[swapped]


def decode(data, texture, canvas=None):
    """RGBA uint8 array. With canvas=(w, h) the image is placed top-left on a transparent canvas."""
    w, h = texture.width, texture.height
    start = texture.pixel_offset + GS_UPLOAD_HEADER
    pal_start = texture.palette_offset + GS_UPLOAD_HEADER
    if texture.format == PSMT4:
        colours, needed = 16, (w * h + 1) // 2
    elif texture.format == PSMT8:
        colours, needed = 256, w * h
    else:
        raise AmtError(f"unsupported PS2 pixel format {texture.format:#x}")
    if texture.pixel_size < needed or texture.palette_size < colours * 4:
        raise AmtError("pixel or palette block smaller than the texture")
    if start + needed > len(data) or pal_start + colours * 4 > len(data):
        raise AmtError(f"texture {texture.id:#x} points past the end of the container "
                       f"(pixels {start:#x}+{needed:#x}, palette {pal_start:#x}, size {len(data):#x})")
    raw = np.frombuffer(data, dtype=np.uint8, count=needed, offset=start)
    palette = np.frombuffer(data, dtype=np.uint8, count=colours * 4, offset=pal_start).reshape(colours, 4).copy()
    palette[:, 3] = np.minimum(255, (palette[:, 3].astype(np.int32) * 255 + 64) // 128)
    if colours == 256:
        palette = _csm1(palette)
        index = raw
    else:
        index = np.empty(needed * 2, dtype=np.uint8)
        index[0::2], index[1::2] = raw & 15, raw >> 4
        index = index[:w * h]
    image = palette[index].reshape(h, w, 4)
    if canvas is None:
        return image
    cw, ch = canvas
    out = np.zeros((ch, cw, 4), dtype=np.uint8)
    out[:min(h, ch), :min(w, cw)] = image[:ch, :cw]
    return out
