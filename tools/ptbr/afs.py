"""CRI AFS archives, read either from a loose file or from inside an ISO image.

Layout (little-endian on both PS2 and Xbox 360): "AFS\\0", u32 count, then count pairs of
(u32 offset, u32 size). The file-name table that may follow is not needed here: entries are matched
by index, which is identical between the PS2 and 360 archives for indices 0..2704.
"""

import os
import struct

from . import iso9660

AFS_MAGIC = b"AFS\x00"


class AfsError(Exception):
    pass


class AfsArchive:
    def __init__(self, fh, base, size, label):
        self.fh, self.base, self.size, self.label = fh, base, size, label
        header = self._read(0, 8)
        if header[:4] != AFS_MAGIC:
            raise AfsError(f"{label}: not an AFS archive")
        count = struct.unpack_from("<I", header, 4)[0]
        table = self._read(8, count * 8)
        self.entries = [struct.unpack_from("<II", table, i * 8) for i in range(count)]

    def _read(self, offset, length):
        self.fh.seek(self.base + offset)
        data = self.fh.read(length)
        if len(data) != length:
            raise AfsError(f"{self.label}: short read at {offset:#x}")
        return data

    def __len__(self):
        return len(self.entries)

    def read(self, index, sector_slack=False):
        """Entry bytes. sector_slack=True also returns the bytes up to the next 0x800 boundary: the PS2
        translation stores a few entries with a size a little short of their real data (the console
        reads whole sectors, so it never noticed)."""
        offset, size = self.entries[index]
        if not size:
            return b""
        if sector_slack:
            size = min((size + 0x7FF) & ~0x7FF, self.size - offset)
        return self._read(offset, size)

    def close(self):
        self.fh.close()


def open_file(path):
    return AfsArchive(open(path, "rb"), 0, os.path.getsize(path), os.path.basename(path))


def open_in_iso(iso_path, inner_path):
    fh = open(iso_path, "rb")
    files = iso9660.list_files(fh)
    if inner_path.upper() not in files:
        fh.close()
        raise AfsError(f"{inner_path} not found in {iso_path}")
    offset, size = files[inner_path.upper()]
    return AfsArchive(fh, offset, size, f"{os.path.basename(iso_path)}::{inner_path}")
