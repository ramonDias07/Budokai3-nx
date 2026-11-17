#!/usr/bin/env python3
"""Extract the supported Budokai 3 game files from a local Xbox 360 disc dump."""

import argparse
from hashlib import file_digest
from pathlib import Path
import tempfile

from extract_iso import SECTOR, ExtractError, extract_file, find_partition, read_volume_descriptor, walk


XEX_SHA256 = "b40bba40cfd6c90cb269ebf5020818924f43109bcc86827a3cc37124c052a26b"
XEX_PATH = "dbz3/yae3_xenon.xex"
GAME_FILES = {
    "adx_jpn.afs", "adx_usa.afs", "data_cmn.afs", "data_fra.afs", "data_spn.afs", "data_usi.afs",
    "data_yah.afs", "ending00.sfd", "ending01.sfd", "lang_jpn.afs", "lang_usa.afs", "opening.sfd",
}


def validate_xex(path):
    with path.open("rb") as executable:
        sha256 = file_digest(executable, "sha256").hexdigest()
    if sha256 != XEX_SHA256:
        raise ExtractError(f"Unsupported Budokai 3 executable (SHA-256 {sha256}). Use the USA/Japan Xbox 360 disc.")


def extract_game(iso, destination):
    if destination.exists():
        validate_xex(destination / "default.xex")
        files = {path.name.lower() for path in (destination / "us").iterdir() if path.is_file()}
        if not GAME_FILES <= files:
            raise ExtractError(f"Incomplete existing extraction: {', '.join(sorted(GAME_FILES - files))}")
        print(f"Using existing dump in {destination}")
        return
    with iso.open("rb") as stream:
        base, _ = find_partition(stream)
        sector, size = read_volume_descriptor(stream, base)
        entries = {path.lower(): (path, entry) for path, entry in walk(stream, base, sector, size)
                   if not entry["is_dir"]}
        required = {XEX_PATH, *(f"dbz3/us/{file}" for file in GAME_FILES)}
        if not required <= entries.keys():
            raise ExtractError("The ISO is missing the expected DBZ3 files from the USA/Japan collection.")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent, prefix="b3-extract-") as temporary:
            game_root = Path(temporary) / "game_root"
            xex = game_root / "default.xex"
            extract_file(stream, base, entries[XEX_PATH][1], xex)
            validate_xex(xex)
            for file in sorted(GAME_FILES):
                path, entry = entries[f"dbz3/us/{file}"]
                if base + entry["sector"] * SECTOR + entry["size"] > iso.stat().st_size:
                    raise ExtractError(f"Incomplete data in {path}")
                extract_file(stream, base, entry, game_root / "us" / path.rsplit("/", 1)[1])
            game_root.rename(destination)
    print(f"Extracted the Budokai 3 executable and {len(GAME_FILES)} data files to {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "assets/game_root")
    args = parser.parse_args()
    try:
        extract_game(args.iso, args.output)
    except (ExtractError, OSError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
