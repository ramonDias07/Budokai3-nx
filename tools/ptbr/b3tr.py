"""B3TR v1: the translation pack read by the Switch port (all fields little-endian).

Header (32 bytes):
  char magic[4] = "B3TR"; u32 version = 1; u32 file_count; u32 entry_count;
  u32 file_table_offset; u32 entry_table_offset; u32 reserved[2] = 0
File table (file_count x 48 bytes):
  char name[32] (NUL-padded, lower case, name under game_root/us/, e.g. "data_usi.afs");
  u32 original_size (byte size of the 360 file the pack was built against); u32 afs_entry_count;
  u32 original_crc32 (CRC32 of the first 64 KiB of that file); u32 reserved = 0
Entry table (entry_count x 32 bytes, sorted by (file_id, entry_index)):
  u16 file_id; u16 codec (0 raw, 1 zstd); u32 entry_index; u64 data_offset (from pack start);
  u32 stored_size; u32 size (decompressed bytes: the complete replacement AFS entry payload, i.e.
  an uncompressed #AZT/#AMB/#ACA the game reads instead of the original entry); u32 crc32 (of the
  decompressed data); u32 reserved = 0
Data: each entry compressed on its own (zstd level 19, single frame with content size), every entry
starting at a 16-byte boundary.
"""

import os
import struct
import zlib

MAGIC = b"B3TR"
VERSION = 1
HEADER = struct.Struct("<4sIIIIIII")
FILE = struct.Struct("<32sIIII")
ENTRY = struct.Struct("<HHIQIIII")
CODEC_RAW, CODEC_ZSTD = 0, 1
PROBE = 64 * 1024


class PackError(Exception):
    pass


def _zstd():
    try:
        import zstandard
        return zstandard
    except ImportError:
        return None


def file_info(path):
    """(original_size, afs_entry_count, original_crc32) of a 360 AFS file."""
    with open(path, "rb") as fh:
        probe = fh.read(PROBE)
    if probe[:4] != b"AFS\x00":
        raise PackError(f"{path}: not an AFS archive")
    return os.path.getsize(path), struct.unpack_from("<I", probe, 4)[0], zlib.crc32(probe)


def write(path, files, entries, level=19, log=print):
    """files: [(name, path_of_360_file)]; entries: {(file_id, entry_index): bytes}.
    Returns stats. Writes to <path>.part and renames at the end."""
    zstd = _zstd()
    codec = CODEC_ZSTD if zstd else CODEC_RAW
    if not zstd:
        log("[pack] python 'zstandard' not available: storing entries uncompressed (codec 0)")
    compressor = zstd.ZstdCompressor(level=level, write_content_size=True, threads=-1) if zstd else None
    keys = sorted(entries)
    file_table = HEADER.size
    entry_table = file_table + FILE.size * len(files)
    position = (entry_table + ENTRY.size * len(keys) + 15) & ~15
    part = str(path) + ".part"
    records = []
    stats = {"raw": 0, "stored": 0}
    with open(part, "wb") as out:
        out.write(bytes(position))
        for file_id, index in keys:
            data = entries[(file_id, index)]
            stored = compressor.compress(data) if compressor else data
            out.seek(position)
            out.write(stored)
            records.append(ENTRY.pack(file_id, codec, index, position, len(stored), len(data),
                                      zlib.crc32(data), 0))
            stats["raw"] += len(data)
            stats["stored"] += len(stored)
            position = (position + len(stored) + 15) & ~15
        out.truncate(position)
        out.seek(0)
        out.write(HEADER.pack(MAGIC, VERSION, len(files), len(keys), file_table, entry_table, 0, 0))
        for name, source in files:
            size, count, crc = file_info(source)
            encoded = name.lower().encode("ascii")
            if len(encoded) >= 32:
                raise PackError(f"file name too long: {name}")
            out.write(FILE.pack(encoded.ljust(32, b"\0"), size, count, crc, 0))
        out.write(b"".join(records))
    os.replace(part, path)
    stats["size"] = os.path.getsize(path)
    stats["codec"] = codec
    return stats


def read(path):
    """Generator-friendly reader: returns (files, entries, fh) where entries are dicts."""
    fh = open(path, "rb")
    magic, version, file_count, entry_count, file_table, entry_table, r0, r1 = HEADER.unpack(fh.read(HEADER.size))
    if magic != MAGIC or version != VERSION:
        raise PackError("not a B3TR v1 pack")
    fh.seek(file_table)
    files = []
    for _ in range(file_count):
        name, size, count, crc, reserved = FILE.unpack(fh.read(FILE.size))
        files.append({"name": name.rstrip(b"\0").decode("ascii"), "original_size": size,
                      "afs_entry_count": count, "original_crc32": crc})
    fh.seek(entry_table)
    entries = []
    for _ in range(entry_count):
        file_id, codec, index, offset, stored, size, crc, reserved = ENTRY.unpack(fh.read(ENTRY.size))
        entries.append({"file_id": file_id, "codec": codec, "entry_index": index, "data_offset": offset,
                        "stored_size": stored, "size": size, "crc32": crc})
    return files, entries, fh


def load(fh, entry):
    fh.seek(entry["data_offset"])
    stored = fh.read(entry["stored_size"])
    if entry["codec"] == CODEC_RAW:
        data = stored
    elif entry["codec"] == CODEC_ZSTD:
        zstd = _zstd()
        if not zstd:
            raise PackError("zstd entry but python 'zstandard' is not available")
        data = zstd.ZstdDecompressor().decompress(stored, max_output_size=entry["size"])
    else:
        raise PackError(f"unknown codec {entry['codec']}")
    if len(data) != entry["size"]:
        raise PackError(f"entry {entry['entry_index']}: size {len(data)} != {entry['size']}")
    if zlib.crc32(data) != entry["crc32"]:
        raise PackError(f"entry {entry['entry_index']}: CRC32 mismatch")
    return data
