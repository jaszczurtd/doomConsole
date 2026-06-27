#!/usr/bin/env python3
"""Combine UF2 files while rewriting global block numbering."""

import argparse
import struct
from pathlib import Path

UF2_BLOCK_SIZE = 512
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30


def read_blocks(path: Path) -> list[bytearray]:
    data = path.read_bytes()
    if len(data) % UF2_BLOCK_SIZE != 0:
        raise SystemExit(f"{path}: size is not a multiple of 512 bytes")

    blocks: list[bytearray] = []
    for offset in range(0, len(data), UF2_BLOCK_SIZE):
        block = bytearray(data[offset : offset + UF2_BLOCK_SIZE])
        magic0, magic1 = struct.unpack_from("<II", block, 0)
        magic_end = struct.unpack_from("<I", block, 508)[0]
        if (
            magic0 != UF2_MAGIC_START0
            or magic1 != UF2_MAGIC_START1
            or magic_end != UF2_MAGIC_END
        ):
            raise SystemExit(f"{path}: invalid UF2 magic at block {offset // 512}")
        blocks.append(block)

    return blocks


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Combine UF2 files and rewrite blockNo/numBlocks fields."
    )
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()

    blocks: list[bytearray] = []
    for input_path in args.inputs:
        blocks.extend(read_blocks(input_path))

    total = len(blocks)
    for index, block in enumerate(blocks):
        struct.pack_into("<II", block, 20, index, total)

    args.output.write_bytes(b"".join(blocks))


if __name__ == "__main__":
    main()
