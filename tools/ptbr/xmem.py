"""Python side of the xmemd helper: builds it when missing and streams buffers through --pipe."""

import os
import struct
import subprocess

XMEM_MAGIC = b"\x0f\xf5\x12\xee"
HERE = os.path.dirname(os.path.abspath(__file__))


class XmemError(Exception):
    pass


def is_compressed(data):
    """Same test as the game (sub_820A4700): magic plus major version 1, minor 3."""
    return len(data) >= 0x30 and data[:4] == XMEM_MAGIC and data[4:6] == b"\x01\x03"


def build(exe_path, clang, libmspack_dir):
    """Compiles xmemd.c with clang against libmspack's lzxd.c (only file needed)."""
    mspack = os.path.join(libmspack_dir, "mspack")
    if not os.path.isfile(os.path.join(mspack, "lzxd.c")):
        raise XmemError(f"libmspack not found at {libmspack_dir}")
    os.makedirs(os.path.dirname(exe_path), exist_ok=True)
    cmd = [clang, "-O2", "-w", "-I" + mspack, "-o", exe_path,
           os.path.join(HERE, "xmemd.c"), os.path.join(mspack, "lzxd.c")]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.isfile(exe_path):
        raise XmemError("xmemd build failed:\n" + result.stderr)


class Decompressor:
    """One long-lived xmemd --pipe process."""

    def __init__(self, exe_path):
        self.proc = subprocess.Popen([exe_path, "--pipe"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def __call__(self, data):
        if not is_compressed(data):
            return data
        self.proc.stdin.write(struct.pack("<I", len(data)) + data)
        self.proc.stdin.flush()
        length = struct.unpack("<I", self._read(4))[0]
        if length == 0xFFFFFFFF:
            raise XmemError("LZX decompression failed")
        return self._read(length)

    def _read(self, n):
        chunks = []
        while n:
            chunk = self.proc.stdout.read(n)
            if not chunk:
                raise XmemError("xmemd exited unexpectedly")
            chunks.append(chunk)
            n -= len(chunk)
        return b"".join(chunks)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait()
