"""Minimal read-only ISO 9660 reader (PS2 DVD images). No Joliet/Rock Ridge, no extraction to disk."""

import struct

SECTOR = 2048


class IsoError(Exception):
    pass


def _walk(fh, lba, size, prefix):
    fh.seek(lba * SECTOR)
    data = fh.read(size)
    pos = 0
    while pos < len(data):
        length = data[pos]
        if length == 0:  # records never cross a sector: skip to the next one
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        record = data[pos:pos + length]
        extent = struct.unpack_from("<I", record, 2)[0]
        extent_size = struct.unpack_from("<I", record, 10)[0]
        flags = record[25]
        name = record[33:33 + record[32]]
        if name not in (b"\x00", b"\x01"):  # "." and ".."
            text = name.decode("latin-1").split(";")[0]
            if flags & 2:
                yield from _walk(fh, extent, extent_size, prefix + text + "/")
            else:
                yield prefix + text, extent * SECTOR, extent_size
        pos += length


def list_files(fh):
    """Returns {"USR/DATA_SPN.AFS": (byte offset, size), ...} with upper-case paths."""
    fh.seek(16 * SECTOR)
    pvd = fh.read(SECTOR)
    if pvd[1:6] != b"CD001":
        raise IsoError("not an ISO 9660 image (no primary volume descriptor)")
    root = pvd[156:190]
    lba = struct.unpack_from("<I", root, 2)[0]
    size = struct.unpack_from("<I", root, 10)[0]
    return {path.upper(): (offset, length) for path, offset, length in _walk(fh, lba, size, "")}
