<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# SCT sectioned flash images

This specification describes offline extraction of the supplied flash bytes and load records. It does
not specify payload decoding. The packet transport and runtime initialization are in [modem-sct.md](modem-sct.md).
The format facts come from an isolated audit of image containers and updater section metadata.

## Text container

Each nonblank line starts with a colon and contains hexadecimal bytes: byte count, two-byte big-endian
address, record type, that many data bytes, and checksum. The sum of all bytes, including the checksum,
is zero modulo 256. Reject invalid hex, incorrect lengths and failed checksums.

| Type | Meaning | Required fields |
| --- | --- | --- |
| 0x00 | Data | Address and payload identify supplied flash bytes |
| 0x04 | Section selector | No data; the address field contains the selector |
| 0x01 | End | No data; address zero |

The initial selector is zero. A data byte's absolute flash address is selector times 65,536 plus the
record address and the byte's offset. Reject data crossing the record's 16-bit address boundary instead
of wrapping it. Section selectors can repeat or arrive out of order. Reject unsupported record types,
missing end records and nonblank records after the end.

The zero-length type-0x04 convention differs from standard Intel HEX extended linear addresses.
Reject type-0x04 records carrying data under this dialect.

## Sparse map

Collect supplied bytes by absolute address. Identical overlapping bytes may be accepted; conflicting
overlaps must fail extraction. This is an integrity policy, not a claim about every updater.

Form maximal contiguous address ranges. Preserve gaps; do not invent padding. A selector change does
not end a range, and an internal record can cross a 64 KiB boundary. Preserve each range's start and
exclusive end address. The supported layout contains one complete load stream per range.

## Load records

Read markers only at record boundaries. A load header contains three unsigned little-endian 16-bit
values: marker, destination word address, and payload word count. Exactly twice that count bytes follow.
The next header follows immediately. Preserve payload bytes unchanged.

| Marker | Memory space | Coding variant |
| --- | --- | ---: |
| 0xA880 | Instruction | 0, uncoded |
| 0xA781 | Data | 0, uncoded |
| 0xA888 | Instruction | 1 |
| 0xA789 | Data | 1 |
| 0xA88A | Instruction | 2 |
| 0xA78B | Data | 2 |
| 0xA88C | Instruction | 3 |
| 0xA78D | Data | 3 |

These are the established markers. Unknown markers fail extraction; searching forward for a recognizable
marker could silently discard supplied bytes. Instruction and data spaces remain distinct. Preserve
the destination as a word-address value without converting it to an invented CPU or flash address.

The word count determines stored payload size for coded and uncoded records. No internal checksum,
decoded size, cipher or compression algorithm has been established. Observed eight-byte alignment of
coded records is not a universal parser requirement.

## Stream trailers

A trailer contains a little-endian 16-bit marker followed by one unsigned little-endian 16-bit value.
It has no word count or payload. Preserve every bit of its value.

| Marker | Meaning of following value |
| --- | --- |
| 0xA382 | Application instruction-entry word address |
| 0xAB85 | Vocoder index |

A trailer ends the range. Reject truncated headers or payloads, ranges without trailers and bytes after
a trailer. Marker-shaped bytes inside a payload have no structural meaning.

## Extraction contract

An extractor preserves the source text SHA-256, every supplied range, individual raw record payloads,
and a manifest. The manifest records range addresses, sizes, hashes and trailer values. For each load
record it records range offset, absolute flash offset, marker, memory space, coding variant, destination
word address, word count, payload size and payload SHA-256.

Reconstruct each range from its headers, payloads and trailer and compare every byte. Compare the full
range collection with the supplied sparse map as well. This validates binary bytes and addresses;
original text capitalization and line endings need not be reproduced.

Tests use newly authored synthetic containers, including gaps, selector changes, payloads containing
marker bytes, conflicting overlaps, bad checksums, unsupported markers and truncated records.
No stock payload, decoded firmware or key enters the repository. Extraction is offline and requires
no serial connection, flash operation or RF. Coded output is labeled raw and coded; it does not establish
codec initialization or microphone routing.
