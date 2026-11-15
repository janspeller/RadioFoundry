#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Back up an AnyTone radio's own codeplug, over its PC mode, before anything is installed.

The radio answers on its USB serial port while it runs its original firmware normally, not in the update mode
(docs/spec/codeplug-stock-anytone.md 1): PROGRAM, then reads of 16 bytes at a time, then END. This tool only ever
reads. It writes nothing to the radio and sends no command but PROGRAM, R and END.

What it saves is the raw bytes, region by region, with a manifest naming each one's address: the channels, zones,
contacts, radio IDs, receive group lists and scan lists, plus the bitmaps that say which records exist. The
bitmaps are read first and only the records they mark as present are fetched, so a radio with fifty channels takes
seconds rather than minutes.

A dump is a copy of your own data. It is not committed anywhere and the default output directory is inside build/,
which git ignores.

  anytone_codeplug.py                      back up, to build/anytone-codeplug/
  anytone_codeplug.py --out DIR            somewhere else
  anytone_codeplug.py --port /dev/ttyACM0  a particular port
  anytone_codeplug.py --self-test          check the logic against a simulated radio, no radio needed

The layout is docs/spec/codeplug-stock-anytone.md 3, which is written from the vendor software and **has not been
checked against a radio**. A region whose address is wrong reads as erased (0xFF) rather than failing, so compare
the counts this prints against what the radio's own menus show before trusting a dump.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "rfoundry"))
from rfoundry.anytone import Program, UpdateError, open_port  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The bitmaps: which records exist (codeplug-stock-anytone.md 3.3). "free" inverts the sense
BITMAPS = {
    "zones": {"addr": 0x024C1300, "bytes": 32, "count": 250},
    "radio_ids": {"addr": 0x024C1320, "bytes": 32, "count": 250},
    "scan_lists": {"addr": 0x024C1340, "bytes": 32, "count": 250},
    "hidden_zones": {"addr": 0x024C1360, "bytes": 32, "count": 250},
    "channels": {"addr": 0x024C1500, "bytes": 500, "count": 4000},
    "rx_groups": {"addr": 0x025C0B10, "bytes": 32, "count": 250},
    "contacts": {"addr": 0x02640000, "bytes": 1250, "count": 10000, "free": True},
}

# The record tables (3). base(n) gives a record's address, size its length
TABLES = {
    # a channel is two halves, 64 bytes each: the second 0x2000 further on
    "channels": {"bitmap": "channels", "size": 64, "halves": True,
             "base": lambda n: 0x00800000 + (n // 128) * 0x40000 + (n % 128) * 64},
    "zone_names": {"bitmap": "zones", "size": 32, "base": lambda n: 0x02540000 + n * 32},
    "zone_members": {"bitmap": "zones", "size": 0x200, "base": lambda n: 0x01000000 + n * 0x200},
    "radio_ids": {"bitmap": "radio_ids", "size": 32, "base": lambda n: 0x02580000 + n * 32},
    "rx_groups": {"bitmap": "rx_groups", "size": 0x200, "base": lambda n: 0x02980000 + n * 0x200},
    "contacts": {"bitmap": "contacts", "size": 100,
             "base": lambda n: 0x02680000 + (n // 1000) * 0x40000 + (n % 1000) * 100},
    "scan_lists": {"bitmap": "scan_lists", "size": 0xB6,
               "base": lambda n: 0x01080000 + (n // 16) * 0x40000 + (n % 16) * 0x200},
}
HALF_OFFSET = 0x2000


def present(bits, spec):
    """The record numbers a bitmap marks as present, least significant bit first"""
    free = spec.get("free", False)
    out = []
    for n in range(spec["count"]):
        byte = n // 8
        if byte >= len(bits):
            break
        on = (bits[byte] >> (n % 8)) & 1
        if bool(on) != free:
            out.append(n)
    return out


def backup(radio, out_dir, log=print):
    """Read the bitmaps, then every record they mark present. Returns the manifest"""
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"regions": [], "counts": {}}

    bits = {}
    for name, spec in BITMAPS.items():
        data = radio.read(spec["addr"], spec["bytes"])
        bits[name] = data
        path = "bitmap_%s.bin" % name
        with open(os.path.join(out_dir, path), "wb") as fh:
            fh.write(data)
        manifest["regions"].append({"file": path, "addr": spec["addr"], "len": len(data)})

    for name, t in TABLES.items():
        spec = BITMAPS[t["bitmap"]]
        nums = present(bits[t["bitmap"]], spec)
        manifest["counts"][name] = len(nums)
        log("%-13s %d record(s)" % (name, len(nums)))
        if not nums:
            continue
        blob = bytearray()
        for n in nums:
            a = t["base"](n)
            blob += radio.read(a, t["size"])
            if t.get("halves"):
                blob += radio.read(a + HALF_OFFSET, t["size"])
        path = "%s.bin" % name
        with open(os.path.join(out_dir, path), "wb") as fh:
            fh.write(blob)
        manifest["regions"].append({"file": path, "records": nums, "record_len": t["size"],
                        "halves": bool(t.get("halves")), "len": len(blob)})

    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1)
    return manifest


def self_test():
    """The bitmap decoding and a whole backup against a simulated radio, no radio needed"""
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL: " + what)

    check(present(bytes([0b00000101]), {"count": 8}) == [0, 2], "a bitmap's set bits, low bit first")
    check(present(bytes([0b11111010]), {"count": 8, "free": True}) == [0, 2], "a 'free' bitmap inverts")
    check(present(bytes([0xFF]), {"count": 4}) == [0, 1, 2, 3], "the count limits the bits read")
    check(present(b"", {"count": 8}) == [], "a short bitmap stops")

    # A radio that answers every read with bytes derived from the address, and one channel and one contact present
    class FakeProgram:
        def __init__(self):
            self.reads = []

        def read(self, addr, n):
            self.reads.append((addr, n))
            if addr == BITMAPS["channels"]["addr"]:
                return bytes([0b00000011]) + bytes(n - 1)		# channels 0 and 1
            if addr == BITMAPS["contacts"]["addr"]:
                return bytes([0xFE]) + b"\xff" * (n - 1)		# contact 0 in use
            if addr == BITMAPS["zones"]["addr"]:
                return bytes([0b00000001]) + bytes(n - 1)		# zone 0
            if addr in (BITMAPS["radio_ids"]["addr"], BITMAPS["scan_lists"]["addr"],
                    BITMAPS["hidden_zones"]["addr"], BITMAPS["rx_groups"]["addr"]):
                return bytes(n)
            return bytes([(addr >> 8) & 0xFF]) * n

    import tempfile
    fake = FakeProgram()
    with tempfile.TemporaryDirectory() as tmp:
        m = backup(fake, tmp, log=lambda *a: None)
        check(m["counts"]["channels"] == 2, "two channels found: %r" % m["counts"]["channels"])
        check(m["counts"]["contacts"] == 1, "one contact found: %r" % m["counts"]["contacts"])
        check(m["counts"]["zone_names"] == 1, "one zone found")
        check(m["counts"]["radio_ids"] == 0, "no radio IDs")
        check(os.path.getsize(os.path.join(tmp, "channels.bin")) == 2 * 128,
              "a channel is two 64-byte halves")
        check(os.path.getsize(os.path.join(tmp, "contacts.bin")) == 100, "a contact is 100 bytes")
        check(os.path.exists(os.path.join(tmp, "manifest.json")), "the manifest is written")
        # the second half of channel 0 is read 0x2000 past the first
        first = TABLES["channels"]["base"](0)
        check((first, 64) in fake.reads and (first + HALF_OFFSET, 64) in fake.reads,
              "both halves of a channel are read")
        # nothing but reads: a backup never writes to the radio
        check(not hasattr(fake, "wrote"), "the backup only reads")

    print("self-test: %d checks failed" % len(fails) if fails else "anytone_codeplug self-test passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="the radio's USB serial port (default: the only one found)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "anytone-codeplug"),
            help="where to write the dump (default: build/anytone-codeplug)")
    ap.add_argument("--self-test", action="store_true", help="check the logic against a simulated radio")
    a = ap.parse_args()
    if a.self_test:
        return self_test()

    try:
        port = open_port(a.port)
    except (UpdateError, OSError) as e:
        print("anytone_codeplug: %s" % e, file=sys.stderr)
        return 2
    radio = Program(port)
    try:
        radio.hello()
        print("the radio is in PC mode; reading (it only ever reads)")
        m = backup(radio, a.out)
    except (UpdateError, OSError) as e:
        print("anytone_codeplug: %s" % e, file=sys.stderr)
        return 1
    finally:
        try:
            radio.end()
        except Exception:
            pass
    total = sum(r["len"] for r in m["regions"])
    print("wrote %s (%d bytes in %d regions)" % (a.out, total, len(m["regions"])))
    print("check these counts against the radio's own menus before trusting the dump")
    return 0


if __name__ == "__main__":
    sys.exit(main())
