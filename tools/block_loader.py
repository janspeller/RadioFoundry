#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""block_loader: write a firmware image through a radio's vendor bootloader that takes the whole application region in
numbered 1 KB blocks over the programming cable. The protocol is docs/spec/hardware-rt4d.md 6.1. Every radio that
speaks it is a board with [flash] family = "serial-block" in its board.toml; --model picks one.

    block_loader.py --model M rfoundry_M.bin                    flash a raw application image (linked for the app base)
    block_loader.py --model M IMAGE --dry-run                   check the image and print the plan, touch nothing
    block_loader.py --model M IMAGE --out FILE                  write the frames that would be sent to FILE, touch nothing
    block_loader.py --list-ports                                the USB-serial cables found
    block_loader.py --self-test                                 against a simulated bootloader, no radio

Bootloader mode: the board's [flash.bootloader] steps, printed before each flash. The bootloader below the application
is never written, so it stays available whatever is flashed.

Needs Python 3.8+ and pyserial (pip install pyserial) for a real flash. Runs on macOS, Linux and Windows.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "rfoundry"))
from rfoundry import boards as rfoundryboards  # noqa: E402

BAUD = 115200
BLOCK = 1024
CHECK_ADD = 0x48			# a frame's last byte: the 8-bit sum of the bytes before it plus this
DATA_CMD = 0x57
ACK, ERR = 0x06, 0xFF
IGNORED = (0x00, 0x32)			# answers the updater skips over
CONNECT_TRIES = 4
CONNECT_GAP = 0.2			# s between connect frames while nothing answers
CONNECT_AGAIN = 2			# connect frames sent after the first answer
FRAME_GAP = 0.05			# s the vendor updater waits after each frame
ANSWER_TIMEOUT = 2.0			# s for the answer to a frame (the bootloader erases before the first block)
START_TIMEOUT = 10.0

# USB-serial bridges used in programming cables: (VID, PID, name)
USB_SERIAL = [
    (0x1A86, 0x7523, "CH340"), (0x1A86, 0x5523, "CH341"), (0x1A86, 0x55D4, "CH9102"),
    (0x0403, 0x6001, "FTDI FT232R"), (0x0403, 0x6015, "FTDI FT231X"), (0x0403, 0x6010, "FTDI FT2232"),
    (0x0403, 0x6014, "FTDI FT232H"), (0x10C4, 0xEA60, "CP210x"), (0x067B, 0x2303, "PL2303"),
    (0x067B, 0x23A3, "PL2303GC"), (0x067B, 0x23C3, "PL2303GT"), (0x067B, 0x23D3, "PL2303GE"),
]


class FlashError(Exception):
    pass


class Model:
    """A radio on this bootloader: its name, update-mode steps, application region and RAM (from the board
    registry)."""

    def __init__(self, id, name, steps, app_base, flash_end, ram=None):
        self.id = id
        self.name = name
        self.steps = list(steps)
        self.app_base = app_base
        self.flash_end = flash_end
        self.ram = tuple(ram) if ram else None	# (start, end) of the main RAM: the linker script's MEMORY RAM

    @property
    def region(self):
        return self.flash_end - self.app_base

    @property
    def blocks(self):
        return self.region // BLOCK


def _models():
    out = {}
    for b in rfoundryboards.BOARDS.values():
        if getattr(b, "family", None) == "serial-block":
            out[b.id] = Model(b.id, b.name, getattr(b, "loader_steps", None) or [], b.app_base, b.app_region[1],
                              getattr(b, "ram_region", None))
    return out


MODELS = _models()


def frame(body):
    """body with its check byte."""
    body = bytes(body)
    return body + bytes([(sum(body) + CHECK_ADD) & 0xFF])


CONNECT = frame(b"\x39\x33\x05\x10")
START = frame(b"\x39\x33\x05\x55")


def data_frame(offset, chunk):
    """One 1028-byte data frame: the command, the offset's bits 15-8 and 7-0 (the field wraps every 64 KB), the
    block, the check byte."""
    if len(chunk) != BLOCK:
        raise ValueError("a data frame carries %d bytes" % BLOCK)
    return frame(bytes([DATA_CMD, (offset >> 8) & 0xFF, offset & 0xFF]) + chunk)


def check_image(raw, model):
    """The image padded to the whole region (the bootloader always takes all of it), or FlashError."""
    if not raw:
        raise FlashError("the image is empty")
    if raw[:4] == b"\x7fELF":
        raise FlashError("that is an ELF file: give the .bin")
    if len(raw) > model.region:
        raise FlashError("the image is %d bytes, the application region %d" % (len(raw), model.region))
    if len(raw) < 8:
        raise FlashError("the image is too short for a vector table")
    sp, reset = struct.unpack_from("<II", raw, 0)
    # A Cortex-M image starts with its stack top in SRAM and its reset vector (Thumb bit set) in the region
    if not 0x20000000 <= sp <= 0x20100000 or not (reset & 1) or not model.app_base <= (reset & ~1) < model.flash_end:
        raise FlashError("the image doesn't start with a vector table for 0x%08X (stack 0x%08X, reset 0x%08X): is it "
                         "linked for the %s?" % (model.app_base, sp, reset, model.name))
    # The bootloader starts the image only if its stack pointer is below the end of RAM: the end itself is refused and
    # the radio stays dark (6.1, Image check). The platform's linker script puts it below (_vector_sp)
    if model.ram and not model.ram[0] <= sp < model.ram[1]:
        raise FlashError("the image's initial stack pointer 0x%08X is not below the end of RAM (0x%08X): the %s's "
                         "bootloader won't start it. Rebuild it with the current linker script"
                         % (sp, model.ram[1], model.name))
    return raw + b"\xff" * (model.region - len(raw))


def frames(image):
    return [data_frame(i * BLOCK, image[i * BLOCK:(i + 1) * BLOCK]) for i in range(len(image) // BLOCK)]


def list_cables():
    try:
        from serial.tools import list_ports
    except ImportError:
        raise FlashError("pyserial is missing: pip install pyserial")
    out = []
    for p in list_ports.comports():
        for vid, pid, name in USB_SERIAL:
            if p.vid == vid and p.pid == pid:
                out.append((p.device, "%s %04X:%04X %s" % (name, vid, pid, p.description or "")))
    return out


def find_port(port=None):
    if port:
        return port
    cables = list_cables()
    if len(cables) == 1:
        return cables[0][0]
    if not cables:
        raise FlashError("no USB-serial programming cable found: plug it in, or give --port")
    raise FlashError("several USB-serial cables found (%s): give --port" % ", ".join(c[0] for c in cables))


class SerialPort:
    def __init__(self, device):
        try:
            import serial
        except ImportError:
            raise FlashError("pyserial is missing: pip install pyserial")
        try:
            self.s = serial.Serial(device, BAUD, bytesize=8, parity="N", stopbits=1, timeout=0.05, write_timeout=5)
        except serial.SerialException as e:
            raise FlashError("can't open %s: %s (close any program using it)" % (device, e))

    def write(self, b):
        self.s.write(b)
        self.s.flush()

    def read(self, n):
        return self.s.read(n)

    def reset_input_buffer(self):
        self.s.reset_input_buffer()

    def close(self):
        self.s.close()


class Flasher:
    """The updater's sequence over a port with write / read / reset_input_buffer. sleep and clock are the time source
    (the self-test passes a simulated one)."""

    def __init__(self, port, log=print, sleep=time.sleep, clock=time.monotonic):
        self.port, self.log, self.sleep, self.clock = port, log, sleep, clock

    def answer(self, timeout, what):
        end = self.clock() + timeout
        while self.clock() < end:
            b = self.port.read(1)
            if not b:
                continue
            if b[0] == ACK:
                return
            if b[0] == ERR:
                raise FlashError("the bootloader refused %s (0xFF)" % what)
            if b[0] in IGNORED:
                continue
            raise FlashError("unexpected answer 0x%02X to %s" % (b[0], what))
        raise FlashError("no answer to %s" % what)

    def connect(self):
        self.port.reset_input_buffer()
        for _ in range(CONNECT_TRIES):
            self.port.write(CONNECT)
            try:
                self.answer(CONNECT_GAP, "connect")
                break
            except FlashError as e:
                if "no answer" not in str(e):
                    raise
        else:
            raise FlashError("the radio doesn't answer: is it in bootloader mode, on the right port?")
        for _ in range(CONNECT_AGAIN):
            self.port.write(CONNECT)
            self.answer(ANSWER_TIMEOUT, "connect")

    def write(self, image):
        fs = frames(image)
        self.connect()
        self.port.write(START)
        self.answer(START_TIMEOUT, "start")
        for i, f in enumerate(fs):
            self.port.write(f)
            self.sleep(FRAME_GAP)
            if i != len(fs) - 1:	# the updater waits for nothing after the last frame
                self.answer(ANSWER_TIMEOUT, "block %d" % i)
            if i % 16 == 15 or i == len(fs) - 1:
                self.log("  %3d / %d blocks" % (i + 1, len(fs)))


class FakeBootloader:
    """The radio's side of 6.1 for the self-test: answers each frame, checks it, keeps the blocks."""

    def __init__(self, blocks, fail_at=None, mute_first=0, noise=False):
        self.blocks, self.fail_at, self.mute, self.noise = blocks, fail_at, mute_first, noise
        self.rx, self.out, self.state, self.connects, self.mem = b"", b"", "idle", 0, {}

    def reset_input_buffer(self):
        self.out = b""

    def read(self, n):
        r, self.out = self.out[:n], self.out[n:]
        return r

    def reply(self, b):
        self.out += (b"\x32" if self.noise else b"") + bytes([b])

    def write(self, b):
        self.rx += b
        while True:
            need = 5 if self.state != "data" else BLOCK + 4
            if len(self.rx) < need:
                return
            f, self.rx = self.rx[:need], self.rx[need:]
            if f[-1] != (sum(f[:-1]) + CHECK_ADD) & 0xFF:
                self.reply(ERR)
                continue
            if f == CONNECT:
                if self.mute:
                    self.mute -= 1
                    continue
                self.connects += 1
                self.reply(ACK)
            elif f == START and self.connects >= 1 + CONNECT_AGAIN:
                self.state = "data"
                self.reply(ACK)
            elif self.state == "data" and f[0] == DATA_CMD:
                n = len(self.mem)
                if (f[1] << 8 | f[2]) != (n * BLOCK) & 0xFFFF:
                    self.reply(ERR)
                    continue
                self.mem[n] = f[3:-1]
                if self.fail_at == n:
                    self.reply(ERR)
                elif n != self.blocks - 1:
                    self.reply(ACK)
            else:
                self.reply(ERR)


def self_test():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL: " + what)

    check(CONNECT == bytes.fromhex("39330510C9"), "connect frame as in the spec")
    check(START == bytes.fromhex("393305550E"), "start frame as in the spec")
    f = data_frame(65 * BLOCK, bytes(range(256)) * 4)
    check(len(f) == 1028 and f[:3] == b"\x57\x04\x00", "data frame header, offset wraps at 64 KB")
    check(f[-1] == (0x57 + 0x04 + sum(range(256)) * 4 + 0x48) & 0xFF, "data frame check byte")
    m = Model("t", "Test radio", ["Hold PTT"], 0x08002800, 0x08040000, (0x20000000, 0x2000C000))
    check(m.blocks == 246 and m.region == 251904, "the region is 246 blocks")
    img = struct.pack("<II", 0x2000B000, 0x08002901) + bytes((i * 7) & 0xFF for i in range(5000))
    padded = check_image(img, m)
    check(len(padded) == m.region and padded[-1] == 0xFF, "padded to the region")
    for bad, what in ((b"", "empty"), (b"\x7fELF" + bytes(100), "ELF"), (b"\x01" * (m.region + 1), "too big"),
                      (struct.pack("<II", 0x08000000, 0x08002901) + bytes(8), "no stack in SRAM"),
                      (struct.pack("<II", 0x2000B000, 0x08000101) + bytes(8), "reset vector below the region"),
                      (struct.pack("<II", 0x2000C000, 0x08002901) + bytes(8), "stack pointer at the end of RAM"),
                      (struct.pack("<II", 0x2000C100, 0x08002901) + bytes(8), "stack pointer past the end of RAM")):
        try:
            check_image(bad, m)
            check(False, "check_image refuses " + what)
        except FlashError:
            pass

    class Clock:
        t = 0.0

        def sleep(self, s):
            self.t += s

        def now(self):
            self.t += 0.001
            return self.t

    def run(**k):
        c = Clock()
        fb = FakeBootloader(m.blocks, **k)
        Flasher(fb, log=lambda *a: None, sleep=c.sleep, clock=c.now).write(padded)
        return fb

    check(len(check_image(struct.pack("<II", 0x2000BFF8, 0x08002901) + bytes(8), m)) == m.region,
          "a stack pointer just below the end of RAM passes")
    for b in MODELS.values():
        check(b.ram is not None, "the registry gives %s's RAM" % b.id)
    fb = run()
    check(b"".join(fb.mem[i] for i in range(m.blocks)) == padded, "every block arrives in order")
    fb = run(mute_first=2, noise=True)
    check(len(fb.mem) == m.blocks, "connect retries and ignored answers")
    for k, what in (({"fail_at": 10}, "a refused block"), ({"mute_first": CONNECT_TRIES}, "a radio that never answers")):
        try:
            run(**k)
            check(False, "stops at " + what)
        except FlashError:
            pass
    print("block_loader self-test: %s" % ("FAILED (%d)" % len(fails) if fails else "ok"))
    return 1 if fails else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Flash a radio through its vendor bootloader's 1 KB block protocol",
                                 epilog=__doc__.split("\n\n", 1)[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="raw application image (.bin)")
    ap.add_argument("--model", choices=sorted(MODELS), help="the radio (a board on this bootloader)")
    ap.add_argument("--port", help="serial port of the programming cable (default: the only USB-serial cable)")
    ap.add_argument("--dry-run", action="store_true", help="check the image and print the plan; touch nothing")
    ap.add_argument("--out", metavar="FILE", help="write the bytes that would be sent to FILE; touch nothing")
    ap.add_argument("--yes", "-y", action="store_true", help="don't wait for Enter before flashing")
    ap.add_argument("--list-ports", action="store_true", help="list the USB-serial cables found")
    ap.add_argument("--self-test", action="store_true", help="run against a simulated bootloader")
    a = ap.parse_args(argv)
    try:
        if a.self_test:
            return self_test()
        if a.list_ports:
            for dev, desc in list_cables():
                print("%-16s %s" % (dev, desc))
            return 0
        if not a.image or not a.model:
            ap.error("give an image and --model")
        model = MODELS[a.model]
        with open(a.image, "rb") as f:
            image = check_image(f.read(), model)
        print("%s: %d bytes as %d blocks of %d at 0x%08X" % (model.name, model.region, model.blocks, BLOCK,
                                                             model.app_base))
        if a.out:
            with open(a.out, "wb") as f:
                f.write(CONNECT * (1 + CONNECT_AGAIN) + START + b"".join(frames(image)))
            print("wrote the frames to %s" % a.out)
            return 0
        if a.dry_run:
            print("dry run: nothing sent")
            return 0
        for i, s in enumerate(model.steps, 1):
            print("  %d. %s" % (i, s))
        if not a.yes:
            input("Press Enter once the radio is in bootloader mode (Ctrl+C to stop) ")
        port = SerialPort(find_port(a.port))
        try:
            Flasher(port).write(image)
        finally:
            port.close()
        print("done: switch the radio off and on")
        return 0
    except FlashError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
