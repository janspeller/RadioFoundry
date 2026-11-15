<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# Read-only NAND replacement log

`drivers/flash/nand_remap` reads a persisted replacement-block log through a board-supplied raw reader.
It has no program, erase or reconstruction operation. The board validates its identification marker before
loading the log and supplies device size and the permitted replacement-block range.

The erase-block size is 128 KiB. Physical block zero contains up to 125 log slots, 1024 bytes apart. Each
504-byte record contains 125 little-endian source block numbers at offset 0, 125 paired replacement block
numbers at offset 250, then marker 0x55, slot index, entry count and a persisted enable byte of zero.
The loader uses the last consecutive marked slot. An absent log is unavailable, not an identity map.
A valid marked record with zero entries is an identity map.

The reader rejects more than 89 entries, mismatched slot indices, invalid headers, out-of-range source or
replacement blocks, self replacements, duplicate sources and duplicate targets. Missing or malformed data
never triggers reconstruction or a guessed mapping. A mapped read replaces the block once and preserves
the offset within it. Reads crossing block boundaries are split and each block is resolved independently.
Address overflow and reads beyond device size are refused. The board serializes raw device access.

See [nand-blocks.md](nand-blocks.md) for the separate writable block layer used by RadioFoundry storage.
