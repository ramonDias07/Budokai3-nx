"""#AMB menu containers (PS2 little-endian, Xbox 360 big-endian).

  0x00 "#AMB", 0x04 u32 0x20, 0x08 0, 0x0C u32 (3 on PS2, 2 on 360), 0x10 u32 block count,
  0x14 u32 offset of the block table (0x20), 0x18..0x1F zero.
  Table: count x 16 bytes (u32 offset, u32 size, u32 kind, u32 0); kind 2 = texture, 9 = animation,
  1 = object, 0xFFFFFFFF = other.
  Blocks follow the table, each at the next 0x20 boundary, in table order; the container ends at the
  0x20 boundary after the last block. No total size is stored anywhere.
PS2 block magics and their 360 counterparts: #AMT -> #AZT, #AMA -> #ACA, #AME -> #ACE, #AMO -> #AWO,
#BFC -> #CFC. The translation only changes #AMT blocks; the animation blocks are identical between
the 360 data_usi.afs and data_spn.afs, so a container keeps all of its non-texture blocks verbatim.
"""

import struct

AMB_MAGIC = b"#AMB"


class AmbError(Exception):
    pass


class Amb:
    def __init__(self, header, records, blocks, endian):
        self.header = bytes(header)   # first 0x20 bytes
        self.records = records        # [(offset, size, kind, reserved)]
        self.blocks = blocks          # [bytes]
        self.endian = endian


def parse(data, endian=">"):
    if data[:4] != AMB_MAGIC:
        raise AmbError("not an #AMB container")
    count, table = struct.unpack_from(endian + "II", data, 0x10)
    if table != 0x20:
        raise AmbError(f"unexpected block table offset {table:#x}")
    records = [struct.unpack_from(endian + "4I", data, table + 16 * k) for k in range(count)]
    blocks = []
    for offset, size, kind, reserved in records:
        if offset + size > len(data):
            raise AmbError("block past the end of the container")
        blocks.append(data[offset:offset + size])
    return Amb(data[:0x20], records, blocks, endian)


def serialize(amb):
    endian = amb.endian
    count = len(amb.blocks)
    position = 0x20 + 16 * count
    out = bytearray(amb.header)
    table = bytearray()
    body = []
    for (offset, size, kind, reserved), block in zip(amb.records, amb.blocks):
        position = (position + 0x1F) & ~0x1F
        table += struct.pack(endian + "4I", position, len(block), kind, reserved)
        body.append((position, block))
        position += len(block)
    position = (position + 0x1F) & ~0x1F
    out += table
    out += bytes(position - len(out))
    for offset, block in body:
        out[offset:offset + len(block)] = block
    struct.pack_into(endian + "II", out, 0x10, count, 0x20)
    return bytes(out)
