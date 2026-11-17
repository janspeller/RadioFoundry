<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AnyTone AT-D878UVII PLUS hardware

Status: facts only. Sections 2 to 6 are read from the stock image, so **read every line in them as (unverified)**
unless section 7 says a radio confirmed it; section 1.1 is read off a board in photographs. The family page is
[hardware-anytone.md](hardware-anytone.md): the baseband, the update modes and what a port needs are there and are
not repeated here; the baseband's protocol and engine are in [modem-sct.md](modem-sct.md). This page records what is
specific to the AT-D878UVII PLUS, and checks the AT-D868UV pin map (hardware-anytone.md section 4) pin by pin
against this radio. The order a port is built and checked is in
[d878uv2-bringup.md](../testing/d878uv2-bringup.md).

**Summary.** The 878UVII PLUS board is the 868 board with four changes: the baseband bring-up drives three more
lines, the LCD has no read strobe, the backlight and the frequency-dependent tuning voltage are on different timers
and pins, and a Bluetooth/APRS daughter board hangs off UART4 with its own reset line. Two things the 868 page
does not record are filled in here: PTT is PA10, and the band and front-end switching is on the AT1846S's GPIO
pins rather than on any MCU pin. Everything else — the
SCT3258 host port, the LCD transport and controller, the keypad matrix, the AT1846S three-wire bus, the SPI NAND,
I2C, the GPS UART, the per-slot PA gate — carries over pin for pin. The calibration layout carries over too, with
wider UHF tables. The receive audio path is traced in 4.6: the AT1846S's audio reaches the speaker only through
the codec and the baseband, the volume knob moves a codec register, and PE0 selects which source the speaker
amplifier amplifies.

## 1. The radio and its image

### 1.1 Chips

Read off the board in the public internal photos of FCC ID T4KD878UVII (an AT-D878UVII PLUS). The same filing's
schematic, block diagram, operational description and parts list are withheld under long-term confidentiality.

| Part | Marking | Notes |
| --- | --- | --- |
| MCU | `GD32F303VGT6` | Cortex-M4, 1 MB flash, 96 KB RAM, no CCM. 100-pin LQFP |
| DMR baseband | `SCT3258TD` | QFN64. Runs DMR layers 1-2 and the AMBE+2 vocoder itself ([modem-sct.md](modem-sct.md)) |
| Audio codec | `AIC3204` | TLV320AIC3204. The MCU has no bus to it: it is reached through the baseband, which also sets up its clocks, routing and converters (4.6) |
| RF transceiver | `AT1846S` | `drivers/rf/at1846s` drives it. One chip: no second receiver. Its own GPIO pins carry the band and front-end switching (4.3) |
| PA | `A5M06` | NXP AFT05MS006 |
| External flash | `TC58CVG1S3H RA1G` | 2 Gbit SPI NAND, and the part the identity check in 4.1 demands |
| GNSS | `AT6558` | The chip an ATGM336H module carries, enabled from the RF chip's gpio2 (4.3). Not fitted to every unit |
| Bluetooth and APRS board | silkscreen `APRS-BT 201208 V2.1` | Its own HDSC `HC32F005` Cortex-M0+ (the "BHT_BOARD" firmware), a CML `CMX865` Bell-202 FSK modem for APRS, and a Bluetooth module carrying a Cypress `CYW20707` and a Nuvoton `NAU88C22` codec. The MCU's link to it is in 4.4 |

### 1.2 The stock image

| Item | Finding |
| --- | --- |
| This page is from | AT-D878UVII PLUS V4.00, payload dated 2025-07-25 |
| Payload | A plain Cortex-M image, no header, linked at 0x08004000. Initial SP 0x20015858, reset vector 0x08004C91, both read from the vector table |
| Length | 0x0E3640 (931,392 bytes) |
| Descriptor file | Names the payload `D878UV_1G.bin`, load address 0x08004000, the same length. The third file carries the model string `D878UV2 V100` |
| MCU | GD32F303VGT6 (Cortex-M4, 1 MB flash, 96 KB RAM); STM32F1-compatible peripherals |
| SCT3258 boot block | At file offset 0x0D9CC6 (address 0x080DDCC6), 2048 bytes, SHA-256 `743515b9f7cd90737c1fff29b652b7558bc5a56487ec38df4fdea8877e7a619a` — byte-identical to the block in the 868, 878 and 578 images (hardware-anytone.md 2.2). It is Sicomm's code and must come from the user's own radio |
| Live interrupt vectors | EXTI0, EXTI9_5, RTC, DMA1 channel 7, USB low-priority, USB wakeup, USART2, UART4, TIM5, TIM6, TIM7. Every other vector points at a common do-nothing handler. **USART1, USART3 and UART5 are not used** |
| File | `D878UVII_V4.00_20250725.CDD`, SHA-256 `8f840cfcfc1bb9acab65429ced211481eeb80079725ec46cf225afce071afeeb` |

### 1.3 Memory map

| Item | Value | From |
| --- | --- | --- |
| Vendor bootloader | 0x08000000-0x08003FFF | Never written by a RadioFoundry image |
| `APP_BASE` | 0x08004000 | The stock image's reset vector is 0x08004C91 |
| Modem boot slot | 0x080FF000-0x08100000 | `__codec_slot`, the address every SCT3258 board uses. On a 1 MB part, its last 4 KB |
| Application region | 0x08004000-0x080FF000, 1004 KB | |
| RAM | 96 KB less 272 bytes, the kept crash record on top | the platform's layout (`platform/gd32f30x/common.ld`) |

Flash is not the constraint; RAM is, at 96 KB with no CCM.

### 1.4 Update and recovery

| Item | Finding |
| --- | --- |
| PC link | Native USB on the MCU, a CDC serial port. The vendor update route is `anytone-update` (921600 baud) |
| Update mode | PTT + PF3 held at power-on; the red LED blinks. **Confirmed on a radio** (7.1) |
| After an update | "MCU reboot": PTT + PF1 at power-on. It erases the codeplug |
| Model string | `D878UV2`. **Confirmed on a radio**: in update mode it answers model `D878UV2`, hardware byte 0, protocol `V100`, the GD variant |
| SWD | Pads marked "AF" beside the MCU. The application disables SWJ with AFIO remap 0x00300400 early in its init (3), so a debugger attaches only with NRST (MCU pin 14) held low. Read-out protection was off on the family's documented units; unconfirmed on current production |
| Baseband update | A separate mode (`#` + the top key) and a vendor tool. The port never writes it |

## 2. How the pins were read

The firmware does not write the GPIO control registers directly. It carries a standard STM32F1-style GPIO library:
an init call taking a port base and a three-field struct (16-bit pin mask, 8-bit speed, 8-bit mode) plus single-pin
helpers that write BSRR and BRR. Mode codes are the library's own: 0x00 analog in, 0x04 floating in, 0x10 push-pull
out, 0x14 open-drain out, 0x18 AF push-pull, 0x1C AF open-drain, 0x28 input pull-down, 0x48 input pull-up. Every
board pin therefore resolves from a call site with constant arguments. Speed 3 (50 MHz) is used throughout.

This differs from the 868 only in form. The buses are still bit-banged one pin at a time, with no SPI, I2C or EXMC
peripheral on the LCD, NAND, RF or baseband paths. Where a bus has to change direction, the firmware rewrites the
port's CRL/CRH nibble for that one pin.

Not every board control is an MCU pin. Eight more outputs are the AT1846S's own GPIO pins, driven through that
chip's register 0x1F; they carry the band and front-end switching and the GPS enable (4.3).

Bring-up order, from the board's entry point: tuning PWM, backlight PWM, LCD bus pins, I2C pins, LCD controller
init, splash, AT1846S pins, SPI NAND pins, tone PWM, three periodic timers, GPS UART, NAND identity, NAND feature
registers, then the rest.

## 3. Board init

One function sets the board's static pins:

| Pins | Mode |
| --- | --- |
| PA13, PA14 | AF push-pull, immediately before SWJ is disabled |
| PA1, PA7 | push-pull out |
| PC3, PC4, PC5, PC13 | push-pull out |
| PE0, PE2, PE5–PE14 | push-pull out |
| PA0, PA10, PA14 | input pull-up |
| PA13 | input pull-down |
| PE1, PE15 | input pull-up |
| PB1, PB2 | input pull-up |
| PC5, PC8, PC9, PC14, PC15 | input pull-up (PC5 reconfigured from push-pull out) |

Idle levels at the end of the function: PA7 low, PC3 low, PC4 low, PC13 low, PE0 low, PE6 low, PE15 pulled down,
then a 100 ms delay and PA1 high.

SWJ is turned off with the AFIO remap code 0x00300400 (SWJ fully disabled) and PA13 and PA14 are reused as inputs.
This carries over from the 868: a debugger must connect under reset.

PC14 and PC15 are configured as pull-up inputs, so the low-speed oscillator pins carry no 32.768 kHz crystal on
this board.

## 4. AT-D878UVII PLUS pin map

### 4.1 Buses

| Bus | Pins | Notes |
|---|---|---|
| SCT3258 host port (HPI) | Data PD8–PD15. HCSN PD2, HWRN PD3, HRDN PD4 (active low). HOBIB PD5 (input, low = data waiting) | Intel mode. **Same as the 868.** The data pins switch as a group between push-pull output (CRH 0x33333333) and pull-up input (CRH 0x88888888). Byte write: HCSN low, HWRN low, short wait, data onto PD8–PD15, HWRN high, HCSN high — so data is latched on the rising edge of HWRN. Byte read: HCSN low, HRDN low, wait, read IDR >> 8, HRDN high, HCSN high. A block read holds HCSN low and pulses HRDN per byte. The wait between strobes is a fixed 20-iteration busy loop on a RAM byte |
| SCT3258 control | RSTN PD6. PIO1 strap PC12. Enables PA8, PC6. Also driven in the same sequence: PD1, PD7, PC7 | Reset: PA8 high, PD2/PD3/PD4 high, PD1 low, PD7 low, PC12 low (Intel mode), PC7 low, PC6 high, 100 ms, RSTN high, 100 ms, RSTN low, 500 ms. Then RSTN high and PC12 released to a pulled-down input, and the 1024-word boot block follows over the host port. **The 868's sequence plus PD1, PD7 and PC7**, which the 868 page lists as unidentified outputs. Power-down drives PA8, PD1–PD4, PD6, PD7, PC6, PC7 and PC12 all low |
| LCD (8080, 8-bit) | Data PE7–PE14 (bit 0 on PE7). RS/DC PB10 (low = command). CS PB12, WR PB13 (active low). RESET PB11 | Section 4.2. The byte write reads ODR, masks it with 0x807F and ORs the byte shifted left by 7, so PE0–PE6 and PE15 are untouched. **The 868's RD on PE15 does not exist here**: this board drives no read strobe, and PE15 is something else (4.3) |
| Keypad matrix | Drive lines PE7–PE14 (shared with the LCD data bus). Sense inputs with pull-ups: **PB1, PB2 and PC5 only** | One drive line is pulled low at a time while the others stay high; the three sense lines are read, and a hit is confirmed by a second read after a delay. Keys are active low. **The 868 page lists six sense lines; on this radio only three are read with a row driven.** PA10, PA14, PE1 and PA13 are read on their own with every drive line high, so they are direct inputs rather than matrix positions, and **PB2 is read a second time the same way** — it carries both a matrix column and one direct key (4.3). Which printed key sits where is 4.1.1 |
| Channel knob | PC8, PC9 (quadrature, pull-up inputs) | **Same as the 868.** PC8 raises EXTI line 8 on both edges, IRQ EXTI9_5, preempt priority 9. The two directions enter the same key-event numbering as the keypad, as codes 0x11 and 0x12 (4.1.1) |
| AT1846S | SCLK PB3, SDIO PB4 (bidirectional), SEN PB5 (active low) | **Same as the 868.** Idle: SCLK low, SDIO low, SEN high. Write: SEN low, 8-bit address with bit 7 clear, then 16 bits of data MSB first (high byte then low byte), SEN high. Read: the same address byte with bit 7 set, then SDIO is turned to a pull-up input and 16 bits are clocked in MSB first, sampled while SCLK is high. SDIO direction is changed by rewriting the CRL nibble for PB4. The bit delay is a 5-iteration busy loop |
| SPI NAND | CS PB8, CLK PB14, MOSI/IO0 PB15, MISO/IO1 PA15 | **Same as the 868.** Mode 0, MSB first. PB15's CRH nibble is rewritten between output and pull-up input, so reads can take two bits per clock on PA15 and PB15. The flat byte-address read and write API splits at 2048-byte boundaries, so the page size is 2048 bytes |
| NAND identity | JEDEC 0x9F, probed twice | First with a 1-byte command and 3 bytes read (GigaDevice style): manufacturer 0xC8 sets a style flag. Then with a 2-byte command and 2 bytes read (Toshiba style): manufacturer 0x98 clears it. **The acceptance test then requires manufacturer 0x98 and device 0xCB or 0xEB.** Anything else shows an error screen and the firmware stops in a loop. Writes are issued up to byte address 0x0D9A0000 (about 218 MB), which needs a 2 Gbit device — this is why the 878UVII will not boot on the 1 Gbit part the 868 uses |
| I2C | **SDA PB6, SCL PB7** | Bit-banged, 7-bit addressing. PB6 is the pin that changes direction (CRL nibble 6 between pull-up input and push-pull output), so SDA is PB6. **The 868 page lists these the other way round**; one of the two is wrong and this radio's is PB6 = SDA. Devices addressed: 0x20/0x21 (7-bit 0x10, the RDA5802 FM receiver) and 0x3A/0x3B (7-bit 0x1D, the MMA8653FC accelerometer) — the same two as the 868 |
| GPS UART | USART2: TX PA2 (AF push-pull), RX PA3 (floating input), 9600 baud | **Same pins as the 868** (that page calls them USART1; PA2/PA3 are USART2). RXNE interrupt, NVIC preempt priority 4 |
| BT/APRS UART | UART4: TX PC10 (AF push-pull), RX PC11 (floating input) | New on this radio. Section 4.4 |
| USB | PA11 (D−), PA12 (D+); attach control PE2 | CDC serial. PE2 is driven high when the USB stack attaches and low when it detaches, so it is the D+ pull-up or an equivalent attach switch. At power-off PA11 and PA12 are set to analog |

### 4.1.1 Keypad map

**All of this is read from the image and none of it is confirmed on a radio (unverified).**

**How the scan runs.** One function does the whole matrix. It first drives all eight lines PE7–PE14 low, waits, and
reads PC5, PB1, PB2; if all three are high it reports "no key" at once. Otherwise it reads the same three again
after a second wait to confirm, then walks the rows: for each line in the order **PE7, PE8, PE9, PE10, PE11, PE12,
PE13, PE14** it drives that one line low and the other seven high, waits, and reads **PC5 first, then PB1, then —
only for PE7 and PE8 — PB2**. The first sense line that reads low is confirmed by one more read of the same pin
and the function returns that cell's key code. The wait is the same 100-iteration register busy loop everywhere
(roughly 400 cycles, a few microseconds), used both before the first read and before each confirmation; there is
no debounce timer and no settling time longer than that loop.

Two ordering facts matter for a port:

- Each pass reads the direct inputs first, then the direct PB2 key, and **only scans the matrix when that direct
  PB2 key reads released**. So holding that one key masks the entire keypad.
- The direct PB2 pass is skipped, and its state forced to released, while the last matrix result was code 0x14.

Because the drive lines are the LCD data bus, a scan rewrites PE7–PE14 while the panel is idle. The LCD byte
write masks ODR with 0x807F (4.1), so it does not disturb the sense side, but a scan running between a command
byte and its data byte would corrupt the frame; whether the stock firmware locks the two against each other was
not traced.

**Positions.** Columns are numbered as the scan reads them: PB1 = column 0, PB2 = column 1, PC5 = column 2. Rows
are the drive lines in scan order, PE7 = row 0 … PE14 = row 7. Key codes in parentheses.

| Drive line | PB1 (column 0) | PB2 (column 1) | PC5 (column 2) |
| --- | --- | --- | --- |
| PE7 (row 0) | MENU (0x0D) | P1 (0x13) | `1` (0x02) |
| PE8 (row 1) | up arrow (0x0F) | P2 (0x14) | `2` (0x03) |
| PE9 (row 2) | down arrow (0x10) | unused | `3` (0x04) |
| PE10 (row 3) | `7` (0x08) | unused | `4` (0x05) |
| PE11 (row 4) | `8` (0x09) | unused | `5` (0x06) |
| PE12 (row 5) | `9` (0x0A) | unused | `6` (0x07) |
| PE13 (row 6) | `#` (0x0C) | unused | `0` (0x01) |
| PE14 (row 7) | EXIT (0x0E) | unused | `*` (0x0B) |

18 of the 24 positions are used. **The six unused ones are all of column 1 (PB2) except rows 0 and 1**, and the
firmware does not even read PB2 for rows 2–7.

Read as a board layout — a reading of the table, not something the image states — the wiring falls into a clear
shape: PE7/PE8/PE9 and PE10/PE11/PE12 are two groups of three lines that match the printed keypad's three
columns, and each group serves two printed rows through the two sense lines — `1 2 3` and `4 5 6` on PC5, `7 8 9`
and three of the four keys above the keypad on PB1. PE13 and PE14 carry the remaining four front keys (`0`, `*`,
`#`, EXIT), and PE7/PE8 on PB2 carry the two programmable front keys.

**Key codes.** The codes are small integers, not a bitmap, and the direct inputs and the knob share the same
numbering, so one byte carries every key event:

| Code | Key | Evidence |
| --- | --- | --- |
| 0x01–0x0A | `0` … `9` | One routine turns a code in 1…0x0A into its ASCII character by adding 0x2F, so code 0x01 is `0` and code 0x0A is `9`. The master key switch tests the same 1…0x0A range as "a digit key" |
| 0x0B | `*` | The same routine appends 0x2A for code 0x0B |
| 0x0C | `#` | and 0x23 for code 0x0C |
| 0x0D | MENU | Its own case in the master key switch. On the main screen it clears the menu state and enters the menu's node table |
| 0x0E | EXIT | Its own case. On a sub-screen it writes 0 to the screen id, i.e. back to the main screen |
| 0x0F, 0x10 | up, down | **One shared case** with a direction flag: 0x0F sets it to 0 and 0x10 to 1. The flag goes to the channel step (1 = next channel index), to the list cursor (0 = index − 1) and to the FM-receiver tuning (0x0F tunes up), so 0x0F is up and 0x10 is down |
| 0x11, 0x12 | — | Not keypad: the channel knob's two directions. The Bluetooth/APRS board raises the same two codes (4.4) |
| 0x13, 0x14 | P1, P2 | Programmable, below |
| 0x15, 0x16, 0x17 | PF1, PF2, PF3 | The three direct inputs (4.3) |
| 0x18, 0x19 | PTT pressed, PTT released | PA10 (4.3) |
| 0x1B–0x1D | — | Further events with their own cases in the master switch; not traced |

The digits are confirmed a second way, independently of that routine. The text-entry (multi-tap) tables are
indexed by **code − 1**: index 1 is the punctuation list, 2 `abc`, 3 `def`, 4 `ghi`, 5 `jkl`, 6 `mno`, 7 `pqrs`,
8 `tuv`, 9 `wxyz`, 10 one space, and a parallel table holds the per-index character counts (30, 3, 3, 3, 3, 3, 4,
3, 4, 1). That is the standard phone keypad with punctuation on `1`, so code 0x02 is `1` and code 0x0A is `9`.

**The five programmable keys.** The firmware carries five, and its own menu labels them, in this index order,
**PF1, PF2, PF3, P1, P2**. Their short-press function numbers are a five-byte run in the in-RAM settings record
(offsets +0x10…+0x14 from its base) and the long-press function numbers a second five-byte run (+0x41…+0x45), in
the same order; the long-press time is one more byte (+0x46), used as (value + 1) × 10 timer ticks. Two separate
switches on the key code pick the byte, and both agree:

| Key code | Input | Short-press byte | Long-press byte | Name |
| --- | --- | --- | --- | --- |
| 0x15 | PE1, direct | +0x10 | +0x41 | PF1 |
| 0x16 | PA14, direct | +0x11 | +0x42 | PF2 |
| 0x17 | PB2, direct | +0x12 | +0x43 | PF3 |
| 0x13 | matrix PE7 × PB2 | +0x13 | +0x44 | P1 |
| 0x14 | matrix PE8 × PB2 | +0x14 | +0x45 | P2 |

So **PF1, PF2 and PF3 are the three direct inputs and P1 and P2 are the two matrix positions on PB2.** The name
order comes from the label-pointer table, and the mapping of a label index to a settings offset is the plain
positional one; that last step is an inference, not something the code states (unverified). The AT-D878UVII PLUS
manual labels only PF1, PF2 and PF3, so P1 and P2 are either two front-panel keys this model does not name or a
pair the shared firmware carries for another radio in the family.

Only the three PF keys get long-press treatment: the long-press timer is armed from the direct-input bitmask
masked with 0x0E, which is the three PF bits and not PTT (4.3).

### 4.2 Display — unchanged from the 868

The reset is PB13 high, PB12 high, PB11 high, 100 ms, PB11 low, 100 ms, PB11 high, 500 ms. The controller init is
then, as command/data bytes:

```
11                                  SLPOUT, then 120 ms
B1  01 01 01                        frame rate, normal mode
B2  01 01 01                        frame rate, idle mode
B3  01 01 01 01 01 01               frame rate, partial mode
B4  03                              inversion control
C0  28 08 84                        power 1
C1  C0                              power 2
C2  0C 00                           power 3
C3  8C 2A                           power 4
C4  8A EE                           power 5
C5  0C                              VCOM
36  88                              MADCTL
E0  05 1A 0C 0E 3A 34 2D 2F 2D 2A 2F 3C 00 01 02 10      gamma +
E1  04 1B 0D 0E 2D 29 24 29 28 26 31 3B 00 00 03 12      gamma -
3A  55                              COLMOD, 16-bit RGB565
2C                                  RAMWR
2A  00 00 00 80                     CASET, columns 0..128
2B  00 00 00 A0                     RASET, rows 0..160
```

then 200 ms and display off (0x28); the display is turned on once the first frame is painted. That is an
**ST7735S-class controller, 128×160, portrait, RGB565, MADCTL 0x88** (vertical flip, BGR). The window-setting
routines write 8-bit coordinates with a zero high byte, so no axis exceeds 256 px. Sleep is entered and left with
0x10 and 0x11.

Every value above matches the 868's init. **The larger screen on this radio is not a larger framebuffer**: the
controller class, the resolution, the timing values and the bus pins are the same, so `st7735` with an 8080
parallel transport serves both radios.

### 4.3 Other I/O

| Pin | Use |
| --- | --- |
| PA4 | DAC channel 1 (analog mode): **TX power level**. The value is an 8-bit number shifted left by 4 and written 12-bit right-aligned, so only the top 8 bits of the DAC are used. Same as the 868 |
| PA5 | DAC channel 2 (analog mode), enabled and written from three call sites; what it drives is not identified |
| PA6 | GD32 TIMER2 channel 0 (STM32 TIM3 channel 1), base 0x40000400, period 4095, AF push-pull: a **frequency-dependent analog control voltage** (VCO or front-end tracking; which is not identified). UHF uses 13 points on a 10 MHz grid from 400 MHz; VHF uses 39 points on a 1 MHz grid from 136 MHz. See the tuning-output correction below. The earlier six-point interpretation describes a separate 220 MHz table, not UHF. |
| PA7 | **Speaker amplifier enable**, active high, left low at boot. Set in three places, cleared on nearly every state change. Levels per state: 4.6.3 |
| PE0 | **The amplifier's input select** (4.6.3): high picks the codec's output (the radio's receive audio), low the tone generator and the broadcast receiver. It is low at reset and whenever the amplifier is off, and the paths that drive it low with PA7 high are the RDA5802 broadcast receiver's — which is where the "driven opposite to PA7" reading came from |
| PB0 | **LCD backlight**: TIM8 channel 2 complementary output (CH2N), AF push-pull, period 4095, prescaler 3, PWM mode 2 with both polarities low. A separate call reconfigures PB0 as a plain push-pull output driven low to kill the backlight. The 868 page guesses TIMER2 channel 2 here; on this radio it is TIM8 |
| PB9 | **Tone output**: TIM4 channel 4 PWM, AF push-pull while sounding, switched back to analog when idle. Same as the 868 |
| PC0, PC1, PC2 | ADC1 channels 10, 11, 12, analog mode, scanned continuously into a three-halfword buffer by DMA1 channel 1 in circular mode, sample time 71.5 cycles. **Channel 11 (PC1) is the battery divider**: four samples are averaged and scaled by × 330 × 4 / 4096, giving units of 10 mV, then a calibration byte is added and 100 subtracted. Full scale works out at about 13.2 V, so the divider is about 1:4. The low-battery thresholds are 600 and 610 (6.00 V and 6.10 V). Channel 12 (PC2) is averaged separately and compared against a moving reference with a 50-count hysteresis, then mapped through a table — the shape of a panel potentiometer, most likely the volume control. Channel 10 (PC0) is read as the top 8 bits of its sample and appended to a 1-byte-per-sample ring buffer indexed by a counter, from two periodic paths — a sampled level trace; what it measures is not identified |
| PD0 | Input with pull-up, EXTI line 0, **both edges, NVIC preempt priority 0** (the highest on the board). The handler runs only while transmitting: PD0 high → PA on, PD0 low → PA off. This is the SCT3258 PIO3 slot-timing output, so **the PA is keyed per TDMA slot**. Same as the 868 |
| PC4, PE6 | **The PA gate**, driven together. PA on: PE6 high, PC4 high, then the power ramp. PA off: the ramp down, then PC4 low, PE6 low. Same two pins as the 868 |
| TX power ramp | Two 124-entry 16-bit envelope tables, one rising (0 → 511) and one falling (511 → 0), applied as (entry × target) >> 9 to the TX power DAC with a per-step delay. The step delays default to 500 and 210 loop counts. The ramp runs only in two of the four modulation modes; in the others the DAC is set straight to the target or to 0 |
| PA0 | **Power key**, input with pull-up, read with a 10 ms debounce; the shutdown chain runs while it reads high |
| PA10 | **PTT**, input with pull-up, active low. It is read on its own with every keypad drive line high, so it is wired to ground and is not a matrix position. The scanner confirms a press with a second read 10 ms later and turns it into bit 0 of the direct-input mask, raising key event **0x18** on press and **0x19** on release; a separate predicate that the transmit paths use calls PTT released only when the pin reads high twice 100 ms apart. Every one of those reads is OR-ed with a software PTT flag held in RAM, so a port has to honor both sources |
| PE1 | **PF1** (4.1.1), input with pull-up, active low, read with no drive line low and confirmed by a second read one RTOS tick later. Bit 2 of the direct-input mask; key event **0x15** |
| PA14 | **PF2**, the same, confirmed one RTOS tick later. Bit 3 of the mask; key event **0x16** |
| PB2 | **PF3**, as well as being keypad column 1. After the three pins above, PB2 is read once more; if it is low, its pull-up is re-selected, every drive line PE7–PE14 is driven high, and after a 10-tick RTOS delay it is read a third time. A low then means the key is wired to ground rather than to a drive line. Bit 1 of the mask; key event **0x17**. **While this bit is set the matrix is not scanned at all**, and the pass that sets it is skipped while the last matrix code was 0x14 (P2) |
| PA13 | A direct input but **not a key**. It is read with its ODR cleared, so it uses the internal pull-down and reads high only when something drives it, confirmed by a second read after a short busy-loop delay, and the result is latched in its own byte — it never enters the key-code numbering. Two uses were found: it gates the key task, with one settings byte choosing which of its two states lets keys through, and it selects between two scalings of the audio level that the volume display and the volume setter use. That is the shape of an **accessory or headset detect on the side connector** rather than a panel control; not identified. A change in it also forces a screen redraw |
| PC3 | **Power hold**, push-pull out. Driven low by the board init, set high once the board is up, and cleared only on the power-off paths. One periodic-timer path clears PC3 when PA0 reads high as a counter expires, which forces the radio off |
| PA1 | **Bluetooth/APRS board reset** (4.4). Set high 100 ms after the board init, and pulsed low for 5 ms to restart the daughter board |
| PE3, PE4, PE5 | **Three indicator LED outputs**, push-pull, all low at init, each with its own state byte and all gated by one "LEDs suppressed" flag. **PE5 is turned on where transmit starts and off where receive resumes, so it is the transmit (red) LED**; PE3 is driven from the receive and call paths, so it is the receive (green) LED; PE4 is driven by the Bluetooth path and the power-off path, so it is most likely the Bluetooth indicator. PE5 is also driven by the battery monitor, and one self-test path turns all three on together |
| PE15 | An output driven by the Bluetooth/APRS module from a state byte, in spite of being configured as a pull-up input by the board init; at power-off it is set to analog. Not identified |
| PB11 | Besides being the LCD reset, it is briefly reconfigured as a pull-down input and sampled twice 50 ms apart, and the result is latched in a byte. A strap or detect line; not identified |
| PB2, PE1 | Read together at power-on, before the normal keypad scan starts; if both read low the firmware sets up a state, turns PC13, PE5 and PE3 on, waits 1 s and stops in a loop. With 4.1.1 that is **PF3 and PF1 held together at power-on** — a service or recovery entry |
| PA9 | Input with pull-up, EXTI line 9, **rising edge only**. A wake or detect line; not identified |
| PC13 | Push-pull out, low at init. Set high on the power-off path, in the Bluetooth module and in the two-key halt path (together with PE3 and PE5); driven low where transmit starts and on one Bluetooth path. Not identified — but it is **not** a band or antenna control: both of those are on the AT1846S, below |
| PE2, PC7 | Outputs with uses given above (USB attach, baseband bring-up) |

**Band and front-end switching is not on the MCU.** No MCU pin selects VHF or UHF and none switches the antenna
path. The AT1846S's own eight GPIO pins do that work: the firmware uses them as plain outputs through the chip's
register 0x1F, two bits per pin (`00` high impedance, `01` the pin's built-in function, `10` drive low, `11` drive
high), keeps a shadow copy of the register in RAM and rewrites the whole register for every change. The value
written when the RF chip is set up is 0x5AE2 — gpio0 low, gpio1 high impedance, gpio2 low, gpio3 high, gpio4
low, gpio5 low, gpio6 and gpio7 built-in function. Frequencies in the comparisons below are in units of 10 Hz, and
the thresholds are plain constants in the code, not calibration.

| AT1846S pin | Use |
| --- | --- |
| gpio0, gpio4 | **Receive band select**, complementary, from the receive frequency: above 300 MHz gpio0 high and gpio4 low, at or below 300 MHz gpio0 low and gpio4 high. Both are driven low together where transmit starts, by one call that clears both fields of the register at once. So they are the two receive front-end enables, and both are off while transmitting |
| gpio5 | **Transmit band select**, from the frequency about to be keyed: high above 300 MHz, low at or below. Set in both transmit paths |
| gpio7 | **A second transmit-path enable**, high only while the transmit frequency is between 165 MHz and 300 MHz inclusive, low outside that window and forced low in the receive path. The stock radio will not key that range, so this is most likely a VHF-high path the board can carry but the firmware does not offer (unverified) |
| gpio2 | **GPS module enable**: driven high immediately before USART2's receive interrupt is enabled and the first command goes out to the receiver, and driven low when the GPS is turned off and on both shutdown paths |
| gpio3 | High at init and in both transmit paths. It goes low only when one settings byte is set and the audio state byte is clear, and it is released through a three-state shadow (on, off, off-pending) whose pending state is cleared by a periodic task in the Bluetooth module, so it is held for a hang time instead of switching at once. Audio routing to the Bluetooth/APRS board is the likeliest use; not identified |
| gpio1 | Driven low from seven places and high from one (entering a menu mode). Not identified |
| gpio6 | Left at its built-in function; never written |

**So receive and transmit at the front end are switched in three groups, not by one line:** the two receive band
enables (gpio0, gpio4) go low where transmit starts; the PA gate (PC4 with PE6) comes up there and is then keyed
per TDMA slot from PD0; and the transmit band select (gpio5, with gpio7 for 165–300 MHz) picks which transmit path
the power goes into. A port therefore needs the AT1846S GPIO register in its band-change path, not just the chip's
band and bandwidth tables.

### 4.4 Bluetooth/APRS board link

New on this radio; the 868 has no such board. The link is a plain asynchronous serial port plus one reset line:

| Item | Finding |
| --- | --- |
| Port | UART4. TX PC10 (AF push-pull, 50 MHz), RX PC11 (floating input) |
| Interrupt | RXNE, NVIC IRQ 52, **preempt priority 1** — above everything except the slot-timing EXTI0 |
| Baud | 115200 in normal use. One path opens the port at 9600 instead, with a different state machine; that is most likely the daughter board's own firmware update |
| Reset | PA1. The board is held in reset until 100 ms after the MCU's GPIO init, and is restarted by driving PA1 low for 5 ms, high for 5 ms, then sending a command |
| Status output | PE15, set when a state byte reaches 2 |
| Daughter-board firmware | The vendor package carries two images, `BHT_BOARD_V2.00_M` (11,316 bytes) and `BHT_BOARD_H_V2.00` (12,364 bytes), so there are two board variants |

The MCU has five usable serial ports and uses three: USART2 for the GPS, UART4 for this board, and native USB for
the PC. USART1, USART3 and UART5 have no handler and no pin configuration. One module uses 0x40005000 as a base to
reach the USB endpoint registers at offset 0xC00; that is USB glue, not UART5.

**Register base.** UART4 is at 0x40004C00. The interrupt reads one byte per RXNE, appends it to a 200-byte buffer
and reloads a 100-tick inter-byte timer; when that timer runs out a periodic task clears the receive index and the
frame flag, so a truncated frame is abandoned rather than merged with the next.

**Two protocols share the port.** One RAM flag selects which parser the received byte goes to.

*Binary frames.* The frame the MCU sends is built in one place and is:

| Offset | Field |
| --- | --- |
| 0 | 0x7E, start |
| 1 | length = payload bytes + 3 |
| 2 | command |
| 3… | payload (the builder takes it as two blocks and concatenates them) |
| last − 1 | checksum: the length byte plus the command byte plus every payload byte, truncated to 8 bits |
| last | 0xEF, end |

Command bytes seen at the call sites: **0xD9** with no payload (sent 3 s after start-up, and again on the restart
path — the identification or version query, since the sender then waits for the board's reply), **0xCA** with no
payload, **0xAF**, **0xD3**, **0xD4** and **0xAE** each with one payload byte, and **0xD8** with nine. Each send
clears a handshake byte first and then polls it for up to 100 iterations, so every frame is acknowledged.

The board's own replies are short. The receive state machine accepts only a first byte it knows and drops anything
else without advancing the index, so it resynchronizes on the next byte:

| First byte | Meaning |
| --- | --- |
| 0x00 | One-byte frame. Sets the handshake byte to 1 — the ACK the senders wait for |
| 0x01, 0x05 | One-byte frames. Set the handshake byte to 2 |
| 0x06 | Marks a frame complete while one particular screen is open |
| 0xCA | Two-byte frame; the second byte follows |

Nothing longer is accepted: the expected-length field is only ever written with 2.

*AT text.* In the other mode the parser accepts a line whose first byte is `+` (0x2B), `O` (0x4F) or `E` (0x45) and
collects up to 200 bytes — unsolicited `+…` notifications, `OK` and `ERROR`. So the Bluetooth part is an
AT-command module and the radio talks to it in text, while the APRS/board part uses the 0x7E/0xEF frames
(unverified).

**Keys and PTT from the board.** The board's keys do not get their own code space: the module raises the same key
events as the panel, and in particular it raises **0x11 and 0x12**, the two codes the channel knob also raises
(4.1.1). Which board key maps to which event, and how PTT from the board is distinguished from PA10, were not
traced; the transmit paths OR every PA10 read with a software PTT flag (4.3), which is the obvious route.

A 30-byte transmit ring (one byte per entry, with duplicate suppression on insert) sits beside the frame builder
and carries single-byte commands to the board, one of which is 0xD8.

### 4.5 Differences from the AT-D868UV

| Area | 868 | 878UVII PLUS |
|---|---|---|
| SCT3258 host port, control, boot block | section 4.1 | the same, plus PD1, PD7 and PC7 driven low during reset and power-down |
| LCD transport | data PE7–PE14, DC PB10, CS PB12, WR PB13, RD PE15 held high, RESET PB11 | the same **without RD**; PE15 has another use |
| LCD controller and panel | ST7735S-class, 128×160, MADCTL 0x88 | identical, value for value |
| Keypad, knob | PE7–PE14 drive; PA10, PA14, PE1, PB1, PB2, PC5 sense; PC8/PC9 knob | same pins, but only PB1, PB2 and PC5 are matrix sense lines, and PB2 is read a second time as a direct input. PA10 (PTT), PA13, PA14 and PE1 are direct inputs read with no row driven. The printed key at each position is in 4.1.1 |
| Keypad map | not recorded | 18 of 24 positions used; PF1 = PE1, PF2 = PA14, PF3 = PB2 direct (4.1.1) |
| PTT | not recorded | PA10, active low, direct; OR-ed with a software PTT flag |
| Band and front-end switching | not recorded | AT1846S GPIO pins through register 0x1F; no MCU pin is involved (4.3) |
| LEDs | PE3–PE5, function not identified | PE5 transmit, PE3 receive, PE4 most likely Bluetooth |
| AT1846S, SPI NAND | PB3/PB4/PB5; PB8/PB14/PB15/PA15 | identical |
| NAND part | 1 Gbit, manufacturer 0xC8 or 0x98 accepted | 2 Gbit required: manufacturer 0x98, device 0xCB or 0xEB |
| I2C | the page says SCL PB6, SDA PB7 | SDA PB6, SCL PB7; same two devices (0x10, 0x1D) |
| TX power DAC, PA gate, slot keying | PA4; PC4 and PE6; PD0 | identical, plus the two 124-step power ramp tables |
| Backlight | not identified (PA6 or PB0 guessed) | PB0, TIM8 channel 2 complementary output |
| Tuning voltage | not identified | PA6, TIM3 channel 1, from a frequency-indexed calibration table |
| Serial | USART1 (GPS) only | USART2 (GPS, 9600) and UART4 (BT/APRS board, 115200) |
| Extra lines | — | PA1 daughter-board reset, PE2 USB attach, PE15 BT status |

### 4.6 Receive audio path

All of this is read from the image and **none of it is confirmed on a radio (unverified)**.

**Where the audio goes.** The AT1846S's demodulated audio never reaches the speaker amplifier on its own, in FM or
in DMR. It goes into the TLV320AIC3204, over I2S to the SCT3258, back over I2S to the codec's DAC and out of the
codec's **HPL** driver, through the PE0 switch and into the speaker amplifier that PA7 enables:

```
AT1846S audio out -> AIC3204 analog input (right MICPGA) -> I2S -> SCT3258 -> I2S
  -> AIC3204 DAC -> HPL driver -> PE0 high -> speaker amplifier (PA7 high) -> speaker
```

Transmit runs the same loop the other way: the codec's **LOL/LOR** line outputs feed the AT1846S's microphone
input, which is why the AT1846S's voice source is left on "microphone" (4.6.4).

| Evidence | What it shows |
| --- | --- |
| The volume knob sets the codec's HPL driver gain, nothing else (4.6.2) | The level the user hears is produced in the codec, so the audio the user hears comes out of the codec |
| The AT1846S's own receive gain (reg 0x44) is a fixed per-mode value from a five-entry settings table, never the knob (4.6.4) | The AT1846S is a fixed-gain stage in the chain, not the volume control |
| No MCU ADC or DAC is on an audio line (4.3: ADC1 is battery, the knob and one level trace; DAC1 is TX power) | The MCU is not in the audio path at all; the only digital path between the RF chip and the speaker is codec + SCT3258 |
| The SCT3258 carries analog-mode fields (`ANALOG_CONFIG`, `ANALOG_SUBAUDIO`, `CTC_PARAM`, `SQ_LEVEL`, gains — hardware-anytone.md 2.5) and its vocoder selection has an "analog" value | The baseband processes analog FM audio, which it can only get over its I2S link to the codec |
| The codec's left input chain (IN1L or IN2L routing, left MICPGA volume, left AGC) is set separately from the right MICPGA volume, and only the left one gets AGC | Left channel = microphone, right channel = the RF chip's audio. This split is a reading of the register set, not something the image states |
| The firmware never writes the codec's clock, DAC, ADC or routing registers (4.6.2) | The SCT3258's own firmware sets the codec up over its I2C master; the MCU only trims gains |

**4.6.1 How the MCU reaches the codec.** Only through the SCT3258, as **codec-bypass packets**: TYPE 0x40 (bit 6
set, packet type 0), one field with id 0x00 and a five-byte payload

| Payload byte | Meaning |
| --- | --- |
| 0 | codec page (0 or 1) |
| 1 | codec register |
| 2 | value |
| 3, 4 | zero |

so one packet is one `page / register = value` write. 22 call sites build such a packet, each with the page and
register as constants in a literal pool and the value either a constant or an argument; that is the "about twenty
codec-bypass packets" the family page records. One more call site takes the three bytes from a host command, so the
PC tool can write any codec register.

Normal startup sends TYPE 0 `CODEC_SELECT` (field 0x2D), value 1, after the boot-block ACK and waits for
zero status before vocoder selection. The earlier claim that this sender had no reachable caller was wrong.
The MCU then adjusts codec registers through the bypass packets above. No live `SET_I2C_ADDRESS` sequence
has been established.

**4.6.2 The codec registers the firmware writes.** Register names are the TLV320AIC3204 datasheet's (SLOS602).

| Page | Reg | Value written | Datasheet register | Sent from |
| --- | --- | --- | --- | --- |
| 1 | 0x10 | gain & 0x3F (bit 6, mute, always cleared) or 0x40 (muted) | HPL Driver Gain Setting | **The volume.** Audio parameter 0x0D |
| 1 | 0x11 | 0x40, and in one path a caller's gain | HPR Driver Gain Setting | present, no live caller found: HPR is left muted |
| 1 | 0x12 | caller's byte | LOL Driver Gain Setting | Audio parameter 0x0A |
| 1 | 0x13 | caller's byte | LOR Driver Gain Setting | Audio parameter 0x0B |
| 1 | 0x09 | 0x20, 0x0C or 0x2C (HPL only; LOL+LOR; all three) | Output Driver Power Control | three helpers, **no live caller**: the output drivers are powered up by the baseband's own codec setup |
| 1 | 0x34 | 0x80 (IN1L) or 0x20 (IN2L), 0x00 to unroute | Left MICPGA Positive Terminal Input Routing | microphone select, with the register below |
| 1 | 0x3B | caller's byte | Left MICPGA Volume Control | **microphone gain.** Audio parameter 0x0E, together with 0x34 |
| 1 | 0x3C | caller's byte | Right MICPGA Volume Control | Audio parameter 0x0C. No AGC is set for this channel |
| 0 | 0x56 | caller's byte | Left Channel AGC Control 1 | microphone AGC |
| 0 | 0x58 | caller's byte | Left Channel AGC Control 3 | microphone AGC maximum gain |

Three things to take from the table. The **mute** is the HPL driver's own bit 6: every gain write the firmware makes
clears it, and the firmware has no other way to mute that driver. The **volume** is the HPL driver gain, 6-bit
two's complement, −6 dB … +29 dB. And there is **no output routing, clocking, DAC or ADC setup at all** — the MCU
inherits whatever the SCT3258 left.

**When they are sent.** Not per call and not per mode change:

| Moment | What is sent |
| --- | --- |
| Once, in the boot sequence, after the SCT3258 is booted and immediately before the AT1846S init | The whole audio bring-up: LOL gain, LOR gain, right MICPGA volume, the HPL gain for the current volume step, the microphone routing and gain, plus about fifteen non-codec baseband audio parameters. The five gain bytes come from a RAM record that the settings/calibration reader fills (offsets +0x4D… in the record at 0x2000A050), so they are per-radio values, not constants |
| Every change of the volume knob, and every path that re-applies the volume | One codec-bypass packet: page 1 register 0x10 = the mapped gain |
| Squelch opening or closing, a DMR call starting or ending | **Nothing.** The gate is PA7 (and PE0), not the codec |

**The volume map.** Three stages, all in the MCU:

1. The knob's averaged ADC reading (4.3, ADC1 channel 12) becomes a step 0–15: at or below 0x78 step 0, at or
   above 0xF60 step 15, otherwise `(raw − 0x78) / 0x112 + 1`.
2. Step 0 maps to 0; steps 1–15 index a 15-byte row of a 9-row table at 0x080DDC20, the row chosen by a settings
   byte (one byte for one mode and another for the other, with a ninth row — offset +0x78 — forced in one state).
   The rows rise from `00 02 04 06 09 0B 0C 0D 0E 0F 10 11 12 13 14` to
   `00 09 0E 12 16 18 1A 1C 1D 1E 1F 20 21 22 23`, so the table is a set of volume curves with different maxima.
3. That 0…0x23 value indexes a 36-byte table at 0x080DDAF2 which holds `3A 3B 3C 3D 3E 3F 00 01 02 … 1D`, i.e.
   −6 dB … +29 dB in 1 dB steps as the codec's 6-bit two's complement. The result is the byte written to page 1
   register 0x10.

So volume step 0 gives −6 dB **and** PA7 low; the firmware never relies on the codec alone to go quiet.

**4.6.3 PA7 and PE0.**

PA7 enables the speaker amplifier, active high, as 4.3 records. PE0 selects what the amplifier amplifies. PE0 is
driven high only by the audio manager, and only for the radio's own audio; every other path that touches it drives
it low, and the two paths that drive PE0 low with PA7 high belong to the RDA5802 FM-broadcast receiver (its
preset list, tuning and its own volume writes sit in the same module). The reading that fits every site is:

| PE0 | Amplifier input |
| --- | --- |
| high | the TLV320AIC3204's HPL output — the radio's receive audio, FM and DMR, and anything the baseband plays |
| low | the other analog source: the MCU's own PB9 PWM tone generator and the RDA5802 broadcast receiver. Also the level at reset and whenever the amplifier is off |

That PE0 is one 2:1 analog switch rather than two things is an inference from the call sites, not something the
image states (unverified).

Levels, with the conditions the firmware actually tests:

| State | PA7 | PE0 |
| --- | --- | --- |
| Board init, before anything else | low | low |
| Idle, FM receive squelched, DMR receive with no call | low | low |
| FM receive, squelch open | **high** | **high** |
| DMR receive, call audio | **high** | **high** |
| Receive audio open but the sink is the Bluetooth board (the route byte = 4) | high | low |
| A tone, beep or voice prompt sounding | high | low |
| Broadcast FM (RDA5802) playing | high | low |
| Transmit | low | low (the transmit entry clears the audio state, and the manager follows) |
| Power-off | low | low |

The gate itself is one state byte. The squelch and sub-audio decision for the current channel is reduced to a
single "audio open" byte, the manager copies it, and from it PA7 and PE0 follow on the next pass of the periodic
task. There is no separate unmute sequence, no delay and nothing sent to the codec or the baseband when the
squelch opens. The amplifier is raised through one helper that holds PA7 low only when a settings byte and two
state bytes all say "suppressed"; otherwise it goes high.

**4.6.4 The AT1846S's audio registers.** Entering a channel, the firmware writes, in this order (register = value,
through the chip's three-wire bus; the register pair is the same write function used everywhere):

| Register | FM | DMR | Note |
| --- | --- | --- | --- |
| 0x3A bits 14:12 | 100 | 100 | voice_sel = microphone, the TX modulator source. Also the value the bandwidth tables leave (0x40C3). The 868-family radios differ from the HR_C6000 radios here, which use 000: on this board the modulator audio arrives on the AT1846S's microphone pin from the codec's line outputs |
| 0x58 | 0x8085 | 0x80E5 | audio filters |
| 0x40 | 0x0030 | 0x0031 | bit 0 passes low-frequency audio: set for 4FSK, clear for FM, as on the HR_C6000 radios |
| 0x44 | one of 0x07F9, 0x08F9, 0x09F9, 0x0AF9, 0x0CF9 | 0x0AFA | **receive volume / AF gain.** FM takes a five-entry halfword table at 0x080AF204 indexed by a settings byte; DMR is the one constant |
| bandwidth table | 12.5 kHz or 25 kHz | the same | includes 0x3A = 0x40C3, 0x32, 0x33, 0x34, 0x35, 0x36, 0x3C, 0x3F, 0x41, 0x48, 0x50, 0x56, 0x60, 0x62, 0x65, 0x66 and the page-1 AGC table |
| 0x30 | idle value, then the RX value | the same | 12.5 kHz 0x4006 then 0x4026; 25 kHz 0x7006 then 0x7026. One settings byte adds bit 4 to both (0x4016/0x4036), and the same byte makes AT1846S gpio3 follow the audio state |

In reg 0x44 the low nibble is 9 in FM and 0xA in DMR, bits 7:4 are 0xF in both (full AF gain in the field our
AT1846S spec calls dac_vgain) and bits 11:8 carry the five FM steps 7, 8, 9, 0xA, 0xC. **The volume knob does not
reach this register**: the index is a settings byte, so 0x44 is a per-mode trim, the equivalent of the fixed
0x06CC / 0x07EF the HR_C6000 radios use ([hardware-dm1701.md](hardware-dm1701.md) 5.5).

Reg 0x30 bit 7 (mute) is read from a RAM flag that **nothing in the image ever writes**, so the mute paths
(0x4086/0x40A6 and the 25 kHz pair) are dead and receive always runs unmuted. Note also that 12.5 kHz here is
0x4006/0x4026, not the 0x6006/0x6026 the HR_C6000 radios use: this board clears both bandwidth bits for 12.5 kHz.

**4.6.5 What a silent port is missing.** A port that tunes the AT1846S, sees RSSI rise, opens the squelch and
raises PA7 hears nothing because the two stages after the RF chip are untouched:

1. **PE0 is low.** PA7 high with PE0 low points the amplifier at the tone/broadcast input, not at the codec. In the
   stock firmware receive audio has **PE0 high and PA7 high together**; the only paths that pair PE0 low with PA7
   high are the broadcast receiver's.
2. **The codec's HPL driver is never written**, so it keeps whatever gain and mute bit the SCT3258's own codec
   setup left. Every gain write the stock firmware makes clears the mute bit; a port that writes nothing has no
   volume control and may be muted outright.
3. **The audio bring-up never ran**, so the LOL/LOR gains, the right MICPGA volume (the RF chip's input to the
   codec), the microphone routing and gain and the baseband's own audio parameters are all at the baseband's
   defaults.

The AT1846S side (0x44, 0x40, 0x58, 0x30) is the least likely cause: in FM its values are within a few dB of the
HR_C6000 radios' and the mute bit is never set.

## 5. Calibration and settings in the NAND

**The 868's addresses hold.** Both the calibration block and the settings record are kept as an A/B pair, and the
mirror offset is **0x20000** (128 KB), computed in code rather than stored as a second constant — which is why the
868's fallback addresses do not appear as literals in this image.

| Block | Primary | Fallback |
|---|---|---|
| Calibration | 0x02F60000 | 0x02F80000 |
| Radio settings | 0x02501000 | 0x02521000 |

Each 128 KB block carries two 4-byte tags in its last eight bytes:

| Offset in block | Written | Meaning |
|---|---|---|
| +0x1FFFC | `55 55 AA AA` | written to the new copy **after its data**, making it eligible as the active copy |
| +0x1FFF8 | `AA AA 55 55` | written to the previous copy after the replacement is ready, retiring the previous copy |

**Corrected by the 2026-11-16 audit.** The active-copy predicate requires the +0x1FFFC marker
to match and the +0x1FFF8 retirement marker not to match. The calibration loader selects the
primary when this predicate is true; otherwise it selects the mirror without another predicate
check. The previous description of +0x1FFF8 as a completion tag was incorrect. These are
logical addresses: the stock NAND block-remapping layer described below can change their
physical locations.

The calibration block is about 1.5 KB of small records at fixed offsets from 0x02F60000:

| Offset | Length | Contents |
|---|---|---|
| +0x0000 | 22 | per-band record |
| +0x0020 | 22 | per-band record |
| +0x0080 | 39 | 39-entry table |
| +0x00C0 | 39 | 39-entry table |
| +0x0100 | 9 | small record |
| +0x0140, +0x0160, +0x0180, +0x01A0 | 12 each | four small tables |
| +0x0200, +0x0280, +0x0300, +0x0380 | **121 each** | four 121-entry tables, one per TX power level |
| +0x0400, +0x0440, +0x0480, +0x04C0 | 39 each | four 39-entry tables, one per TX power level |
| +0x0520 | 25 | |
| +0x05A0 | 20 | |
| +0x05C0 | 30 | |
| +0x0600 | 32 | |

The band tables are one value per MHz, as on the 868: **39 entries = 136–174 MHz**, and **121 entries = 400–520
MHz**. The 868 has 81-entry UHF tables (400–480 MHz), so this is a real difference — a reader written for the 868
would stop 40 MHz short. The TX power DAC value is taken from these tables.

Other fixed NAND regions written by the firmware, for orientation (not calibration): 0x024C0000–0x024C8000 and
0x02500000–0x02504400 hold settings and lists, 0x02540000–0x02980000 hold further records, 0x02FA0000, 0x02FC0000
and 0x02FE0000 hold three more small blocks, and writes reach as far as 0x0D9A0000.

A port must read this calibration from the radio's own NAND and never overwrite it, and must honor the A/B tags.

## 6. Open questions

1. Everything on this page, on a radio.
2. What PA5 (DAC channel 2) drives, and whether PA6's tuning voltage goes to the VCO or a tracking filter.
3. What PE15, PB11, PA9 and PC13 do, and whether PE4 is the Bluetooth indicator. PE0 is read as the amplifier's
   input select in 4.6.3; that reading needs a radio and, if it is one 2:1 switch, which source sits on each side.
4. What ADC channel 10 (PC0) measures: its samples go into a ring buffer, but the consumer was not traced.
5. What the two-key power-on path (PF3 + PF1, 4.3) does once it is entered.
6. The meaning of each Bluetooth/APRS command byte over UART4 (4.4), which board key raises which key event, and
   whether the 9600 path is its firmware update.
7. Whether the NAND identity check can be relaxed: the firmware probes GigaDevice style but accepts only
   manufacturer 0x98 with device 0xCB or 0xEB.
8. The remaining fields of the calibration block (the records at +0x0100, +0x0140…+0x01A0, +0x0520…+0x0600).
9. What AT1846S gpio1 and gpio3 switch, and whether gpio7's 165–300 MHz window is a real path on this board.
10. Whether the 868 really scans six keypad sense lines ([hardware-anytone.md](hardware-anytone.md) 4.1): on this
    radio only PB1, PB2 and PC5 are matrix sense lines, and the direct inputs are PA10, PA13, PA14, PE1 and PB2.
11. What PA13 detects (4.3), and which state of it the stock firmware treats as "accessory present".
12. Whether the keys the firmware calls P1 and P2 (4.1.1) exist on the AT-D878UVII PLUS front panel, and whether
    the label-index-to-settings-offset mapping that names PF1/PF2/PF3 is the positional one assumed there.
13. Whether a keypad scan has to be locked against LCD writes, since both drive PE7–PE14 (4.1.1).
14. The receive audio path (4.6), on a radio: that PE0 high and PA7 high together are what makes FM audio
    audible, that the codec's HPL driver is what the volume knob moves, and whether the codec needs anything
    beyond the gains once the SCT3258 has booted.
15. Which settings byte selects the FM value of AT1846S reg 0x44 (4.6.4), which byte adds reg 0x30 bit 4, and
    which menu item each is.
16. What the codec's LOL/LOR line outputs feed. The modulator input of the AT1846S is the reading in 4.6; the
    accessory connector is the other candidate.
17. Whether `CODEC_SELECT`, `SET_I2C_ADDRESS` and `I2C_OPERATION` are used in any other firmware version of this
    family: this image reaches the codec only with codec-bypass packets (4.6.1).

## 7. Stock codeplug, read on a radio

The family's stock codeplug layout ([codeplug-stock-anytone.md](codeplug-stock-anytone.md)) was confirmed on an
AT-D878UVII PLUS running stock 4.00, with `tools/anytone_codeplug.py`. This is the only part of this radio's
documentation that has been checked against hardware.

| What | Result |
| --- | --- |
| PC mode (`PROGRAM`, `R`, `END` on the USB serial port) | answers as that page describes |
| The bitmaps, including the contacts' inverted "free" bit | at the right addresses: the counts matched the radio's own menus - 104 channels, 13 zones, 3 radio IDs, 4 receive group lists, 15 contacts, 10 scan lists |
| Channels | 128 bytes in two halves 0x2000 apart, a readable name at 0x23: 104 of 104 |
| Zone names | 32-byte records, the name at 0: 13 of 13 |
| Radio IDs | 8 BCD digits at 0, a name at 5: 3 of 3 |
| Zone members, receive group lists, contacts, scan lists | the right record counts; contents not inspected |

Nothing has been written back to a radio.

### 7.1 What a RadioFoundry image has confirmed

From a radio running this port (its diagnostics page), beyond the codeplug read above:

| Fact | Evidence |
| --- | --- |
| **The baseband boots.** The reset sequence of 4.1, the 8-bit parallel host port and the 1024-word boot block all work as recorded | the boot reports `ok`, so the chip answered after the block and took the voice selection |
| The SPI NAND and its identity | reads `98CB`: manufacturer 0x98 with device 0xCB, which is what the stock firmware's own check demands (4.1) |
| The band and front-end switching through the RF chip's GPIO register | on a 145.5 MHz channel the shadow reads 0x9BF2: gpio0 low and gpio4 high (the VHF receive enables), gpio7 low, gpio2 high for the GPS, gpio6 left at its built-in function |
| The battery divider and the panel potentiometer | 8275 mV at a full pack, and the potentiometer reads across its travel |
| The keypad, the knob and the power key as inputs | the raw matrix, `knob` and `power` readings move as expected |
| Receive on the AT1846S | RSSI and the noise reading follow the band; the squelch opens |
| **The core's storage on the NAND** | a whole codeplug was written through the CPS and read back identical, so the sector layer mounts, wears and reads in RadioFoundry's own blocks. The factory mark it treats as blank (ten 0x55 bytes) holds on this radio too |
| The open-firmware CPS | answers on the USB serial port: identity, and a whole codeplug written and verified |
| **The factory calibration reads** | only with the chip's on-die ECC turned off for the transfer. A checked read refuses it, which is what a region written without that ECC looks like. With ECC off the remap mounts, the signature at 0x1FF00 matches, and a factory control-DAC word reads back as valid |

**Not confirmed: audio.** FM and DMR receive remain silent after codec gain/unmute writes were added. DMR call
recognition is observed, but speaker output is not. Codec power, routing and mute readback are the next check;
a successful gain-write acknowledgment alone does not establish an audible path. Section 4.6 records the audio
register map and inferred wiring.

## Sources

- FCC ID T4KD878UVII (AT-D878UVII PLUS), internal photos: <https://fccid.io/T4KD878UVII>. The schematic, block
  diagram, operational description and parts list in the same filing are withheld under long-term confidentiality
- Sections 2 to 6 are read from the stock image named in 1.2, under the spec side's rules
  ([CLEANROOM.md](../CLEANROOM.md))
- The family's page, [hardware-anytone.md](hardware-anytone.md), and its sources

### Audio-path follow-up, 2026-11-15

The owner confirms that restoring the receive front end after transmit fixes the RSSI floor problem. FM and
DMR receive audio remain silent; DMR transmit is not recognized by the other radio. Live diagnostics show
three DMR calls played and 118 voice bursts with no lost frames. That establishes DMR packet reception,
not audible playback. The agent did not transmit or start the SDR bench.

The port was still leaving AT1846S register 0x3A's transmit-source bits at 000. Section 4.6.4 requires 100
because the codec modulates through the microphone input. The board now applies this source together with
its mode-specific 0x40, 0x44 and 0x58 trims after bandwidth changes, which overwrite those registers. DMR
transmit now selects the shared driver's DMR-TX state. Returning to receive also retunes the saved receive
frequency, so split RX/TX channels do not remain tuned to their transmit frequency. These corrections are
host-tested and await hardware confirmation; they do not establish a fix for silent receive audio.

The audio-path image with `CONFIG_DIAG` captures codec pages 0 and 1 after voice setup, using raw
`I2C_OPERATION` access to address 0x30 (the AIC3204's seven-bit 0x18 address). Page 0 is restored afterward.
Selected actual register values appear as `board.codec.<page>.<decimal-register>`; -1 means readback failed.
The snapshot includes DAC power and mute, ADC power and mute, HPL/line output routing and power, output gains,
and microphone routing/gain. It is a boot snapshot, so later volume changes are not reflected in it.
It reads the current setup rather than changing routing, clocks or gains. It does not invalidate a successful
baseband boot if readback is unavailable.

The PA6 tuning output remains unimplemented, and PA5's role remains unidentified. No frequency-error or DMR
deviation measurement was made, so the absent DMR transmit indication cannot yet be attributed to frequency.
The FM path also lacks explicit SCT analog-mode control; the documented analog-call sequence needs to be
implemented in the shared protocol layer rather than assuming the DMR engine passes FM audio.

Register meanings: [TI TLV320AIC3204 Application Reference Guide](https://www.ti.com/lit/ml/slaa557/slaa557.pdf).

### DMR receive confirmed; microphone, FM and dual watch follow-up

With the audio-path image, the owner confirms audible DMR receive. DMR transmit is recognized but has no
microphone audio. FM receive and transmit audio are still absent. The first DMR channel after boot shows
"DMR modem off" until a channel change. Codec raw-I2C snapshots all read -1 and provide no register evidence.

The next image connects IN1L to the left MICPGA with page 1 register 0x34 = 0x80 and sets the baseband's digital
microphone/speaker gains to nominal unity. This is the recorded internal microphone input choice; accessory
input selection is not inferred from PA13. The audio setup data now lives in `audio_setup.h` and is exercised
through the actual voice-setup transport by a host test. The codec input and microphone audio still need a
hardware check.

The shared analog engine in [modem-sct.md](modem-sct.md#analog-calls-through-the-baseband) now owns the baseband
on FM channels. Radio callbacks request RX/TX and channel width; the modem task handles acknowledgments.
Returning to receive restores both the saved RF frequency and bandwidth. FM baseband state is exposed as
`board.fm.enabled`, `.ready`, `.tx` (requested direction), `.pending` (field ID), `.error`, `.commands` and
`.timeouts`. Error 0 means no error, 254 send failure, 255 timeout; other values are chip status codes.

The radio core now defers its DMR start while the baseband is booting and starts it when boot completes.
The longer diagnostic boot had exposed a race: the first start failed, and only changing channels retried it.

Software dual watch is enabled through the shared default. A single AT1846S can alternate between channels;
the former `BOARD_UNSUPPORTED DUAL_WATCH` entry was incorrect. This provides one-receiver dual watch, not
simultaneous reception. The new default build fits in 96 KB SRAM. Hardware timing and mixed-mode dual watch
remain untested. The PA6 tuning output remains unimplemented and no transmit frequency/deviation measurement
was made.

### FM mode rejection and microphone routing test

The owner confirms DMR startup and dual watch work with the audio/FM image. DMR receive remains audible, but
DMR microphone audio and both directions of FM audio remain silent. Live FM diagnostics showed error 6 with
no timeout. The engine state identified the refused step as `PROCESS_MODE=0x80`, before any analog config or
RX work-mode command. The next image selects vocoder 16 first and restores vocoder 4 before allowing DMR to
start again. Readiness covers this mode change; scan and dual watch wait while the library loads.
Same-mode changes do not reload the library. Mixed-mode watch can have longer gaps than a watch of two DMR
or two FM channels and still requires hardware timing validation.

Additional view keys are `board.fm.last_field` (decimal command ID), `board.fm.digital_ready`, and
`board.fm.analog_loaded`. Vocoder selection is field 16, process mode field 26. The latest codec snapshots
remain unavailable and do not establish microphone bias, ADC mute or input routing.

With diagnostics enabled, CHIP_REG chip 1/page 1/register 0x34 is a **write-only microphone input selector**.
Only 0x80 (IN1L) and 0x20 (IN2L) are accepted, and changes are refused while the PA is keyed. The returned value
is the requested write, not register readback; a successful bus write does not prove speech input. The boot
profile still selects IN1L. An owner-controlled A/B test can select IN2L without another flash:

```sh
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 1 0x34 0x20 --page 1
# Restore the boot profile's choice:
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 1 0x34 0x80 --page 1
```

This selector does not alter calibration, codec clocks or gains, and it does not start a transmission. Whether
the owner uses the built-in microphone or an external harness must be recorded when comparing input choices.

The vocoder/FM image has now accepted all six FM setup commands on the radio, reporting ready 1, error 0 and
zero timeouts. A receiver-only return to the saved DMR channel restored digital readiness and an active DMR
stack. No transmission or audible FM test was performed by the agent. An IN2L microphone-route request was
issued for an owner-controlled comparison; its speech result is not yet recorded.

### Owner confirms FM receive; transmit remains open

The owner confirms FM receive audio now works, as well as DMR receive, startup and dual watch. With IN2L
requested, FM transmit still has no microphone speech, and the other handheld does not recognize the DMR
transmission reliably enough to assess microphone audio. The agent restored the IN1L request for an FM
comparison. The owner reports FM transmit still fails with IN1L; whether the accessory jack was empty is not
explicitly confirmed.
The current FM engine status had no error or timeout after the owner's tests, but no per-command error
history was captured during transmit.

A separate code audit found that `PIN_SCT_SLOT` (PD0) has no user in the board implementation. The current
`pa_on` holds both PA enables and the calibrated DAC drive throughout a call; there is no DMR burst interrupt
or per-slot PA gating. This is a known implementation gap, not an RF measurement proving the cause of the
unrecognized call. It needs shared burst-gating support and hardware timing validation. The mobile board's
measured on/off delays must not be assumed correct here without checking them. Frequency error, deviation
and the PA6 tuning output remain unverified. Do not infer a microphone failure from an undecodable DMR signal.

### Microphone codec loopback

The owner repeated the FM speech test with the harness removed and the built-in microphone: the other
handheld received a carrier but no speech. The harness is therefore not the sole cause.

The next diagnostic image exposes chip 2, page 0, register 0x18 as the SCT work-mode **loop byte** through
CHIP_REG. Only 0 (normal) and 0x11 (codec loopback) are writable, only when unkeyed and analog receive is ready.
Reads return the requested loop byte, not a hardware register readback. The shared engine selects full-duplex
codec loopback without starting a call. The board refuses RF keying while it is selected. `board.fm.loopback`
shows the selection; `.ready`, `.pending` and `.error` show command completion. A channel or mode request
cancels it. Open monitor to enable the normal speaker path, use low volume and keep PTT released. This is a
microphone-capture check, not a claimed fix for transmit audio. Hardware behavior is untested.

### Stock audio command audit, 2026-11-15

This is a static audit of the V4.00 MCU payload identified in section 1.2. No radio was accessed.
Observed command bytes below have high confidence; their electrical effects remain unverified. The codec's
resident baseband-controlled initialization is not present as an MCU register-write sequence in this image.

The follow-up reachability audit found no established callers of the codec page 1 register 0x09 power
wrappers. Their 0x20, 0x0C and 0x2C payloads are not evidence for adding a power write at PTT. Likewise, no
stock producer of TYPE 0x60 codec readback was established. The normal microphone-enable path selects IN1L
with 0x34 = 0x80 and then writes the left PGA gain. The accessory-state latch affects speaker gain selection;
a microphone route controlled by that latch was not established. No missing reachable codec write has yet
been shown to explain the transmit speech failure.

A complete normal-startup reachability audit supersedes the earlier codec-selection comparison. Stock sends
`CODEC_SELECT = 1` and waits for zero status. It then sends TYPE-5 `DMR_CALL_OPTION`, selects vocoder 4
(AMBE+2), waits for its status, and selects vocoder 16 (Analog), before the normal configuration flush.
The call options configure call/squelch/reporting behavior; they are not evidence of codec/I2S wiring.
RadioFoundry restores the explicit TI codec selection before its vocoder and gain setup. Its mode engine
selects analog on entering FM. This corrects a proven startup omission; a microphone repair remains untested.
The physical converter state and the effect of the remaining startup-order differences are not established.

Stock register 0x48 thresholds differ between bandwidth branches. At saved squelch level 5, the branch
writing RF register 0x30 = 0x4006/0x4046 uses high threshold 42, and the 0x7006/0x7046 branch uses 52.
Both low thresholds are three counts smaller. This does not establish a conversion from hardware detector
thresholds to the low byte of register 0x1B used by RadioFoundry's software squelch. Do not derive a software
noise offset from these constants without verifying that conversion.

- The microphone-enable operation writes codec page 1 register 0x34 = 0x80, then page 1 register 0x3B =
  the configured microphone gain. Its disable operation writes register 0x34 = 0x00 and leaves register
  0x3B alone. The image has callers of both enable and disable through its audio-control dispatcher.
  Therefore the earlier statement that microphone routing never changes per call is too broad. The
  exact call states selecting each operation have not been completely traced.
- Another microphone-routing operation writes register 0x34 = 0x20, but no direct caller of that
  operation was found. IN1L is the input used by the called enable operation. This is evidence for
  the stock software's input choice, not a board-level continuity measurement or a proven accessory
  microphone input mapping. No separate MCU-controlled microphone power GPIO was established.
- The left AGC enable operation sets page 0 register 0x56 to the configured control byte with bit 7
  set, then writes the configured maximum gain to register 0x58. Its disable operation writes the
  configured control byte to register 0x56 without forcing bit 7. Numeric calibrated gain and AGC
  values were not recovered and must not be replaced with another board's values.
- No additional MCU writes establishing ADC power, ADC digital mute, microphone bias, negative
  microphone input routing, DAC power/mute or line-output routing were found in the codec-bypass
  command helpers. Section 4.6.2 remains the available MCU register inventory. These missing writes
  do not prove those hardware functions are disabled: the resident SCT firmware owns codec setup.
- The audio-control parameter dispatcher includes several numbered entries that send nothing.
  The earlier estimate of fifteen additional baseband audio parameters is not an established list
  of fifteen required setup writes. Identified active control fields include digital speaker gain
  (0x69), digital microphone gain (0x68), demodulator gain (0x1C), analog configuration (0x3C), and
  additional settings-dependent fields 0x6B and 0x6C. Their presence alone does not establish
  required values for a minimal audio path.
- Both called analog-configuration construction paths force bit 7 of field 0x3C payload byte 0
  to 1, preserving the other seven bits from settings. This is observed packet construction, not
  an inference from the codec signal path. The port currently sends byte 0 = 0x00 for narrow or
  0x0A for wide, so this bit differs from the stock command. The packet-interface manual calls
  this bit ALT_LPF: additional attenuation at 3 kHz with BB_CH_SEL = 1. It is not a microphone
  enable, and this difference does not establish an explanation for silent transmit.
- The image contains analog command support for PROCESS_MODE = 0x80 followed by WORK_MODE =
  02 00 00, sub-audio field 0x26 and call-start field 0x78 with no payload. No direct
  caller of that combined helper was found, so it is not a confirmed normal-transmit sequence.
  Called control paths also send PROCESS_MODE = 0x82 when the selected vocoder is 16. The
  distinction between 0x80 and 0x82 must be checked against the packet-interface specification
  before changing the port; this audit does not establish that 0x82 fixes silent transmit.

No stock full-duplex codec-loopback use was established. A loud feedback sound in a local codec loop
is not a measurement of the microphone ADC signal or the transmit line-output signal. Raw I2C read
failures likewise provide no evidence for the codec's actual register contents.

### Stock frequency-control DAC audit, 2026-11-15

Static evidence identifies a separate analog control from the PA6 tuning PWM. The first unsigned
16-bit word of the 22-byte calibration record at NAND 0x02F60000, or its valid mirror at 0x02F80000,
is loaded into the record's RAM offset 0. Its calibration-reset default is 1900 (0x076C). The
service editor constrains this value to 0 through 4095 and writes changes immediately to DAC
channel 2, PA5. Normal RF retuning also reapplies this word after updating the PA6 tuning PWM.

The hardware write is the raw, right-aligned count to DAC channel 2's 12-bit holding register
0x40007414. No Hz/ppm conversion, signed offset or addition to an RF frequency word occurs on
this path. DAC channel 1 on PA4 remains the separate PA-power control. PA6's TIM3 PWM uses
frequency-indexed tables and is not this DAC count.

The owner's reported stock FQCU value near 3620 is within this DAC's raw range. Association of
the FQCU display label with this exact stored word has not yet been fully traced, so that name
is provisional. The electrical destination of PA5, the direction and size of its frequency
effect, and the radio's actual saved value still need independent evidence. Do not convert
3620 into Hz or ppm, or substitute it for the radio's stored calibration solely from this audit.

**Byte order and band selection.** The two bytes at calibration offset 0 are copied unchanged
from NAND and read as a native little-endian unsigned halfword. A value of 3620 would therefore
be stored as bytes 0x24, 0x0E. The separate 22-byte record at NAND offset 0x20 is copied to
RAM immediately after the first record; its reset default first word is 1920 (0x0780).
The normal-retune path does not select that second word for DAC2. Its UHF, VHF and other-band
branches all join one common step that applies the first record's offset-0 word to PA5.
The conditional tables in that routine select PA6 PWM counts only.

**Invalid calibration.** The normal calibration reader checks whether the primary has its active marker and lacks
its retirement marker, then chooses the mirror when that predicate is false. The observed reader then copies the selected record
without validating its first word against 0 through 4095. The normal DAC application likewise
has no range check or replacement with 1900. The explicit calibration-reset routine writes
1900 to the first record and 1920 to the second; that is evidence for reset defaults, not
proof of automatic recovery from an erased or out-of-range saved word. A port that rejects
an unavailable, erased or out-of-range first word and uses 1900 adds a defensive policy; it
must not describe that policy as a byte-for-byte reproduction of stock invalid-data handling.

### Applying the saved PA5 control

The port now reads the little-endian word at calibration offset 0 from the selected valid A/B copy,
independently of the PA power-table validity. A value in 0..4095 is applied unchanged to PA5/DAC2 at RF init,
RX tune, TX tune and return to RX. DAC1/PA4 remains zero until PA power is requested. No calibration is written.
Missing, unreadable or out-of-range data does not initialize or write PA5/DAC2. An earlier candidate used
1900, the stock calibration-reset count; that unverified automatic fallback was removed after a receive
regression with factory=0. A reset default does not establish the correct tuning voltage for this radio.

The diagnostic board keys `control_dac.count` and `control_dac.factory` report the selected count and whether
it came from a valid saved word. They are not a voltage or RF frequency measurement. The owner-recalled 3620
is not hard-coded. The FQCU label mapping and carrier-frequency effect remain unconfirmed on hardware.

### Stock transmit RF state and packet lengths, 2026-11-15

The called RF transmit-entry path branches for digital versus analog tone configuration, then
both branches reach the same AT1846S register 0x30 helper. Its writes are:

| Width | Idle write | Transmit write |
| --- | --- | --- |
| Narrow | 0x4006 | 0x4046 |
| Wide | 0x7006 | 0x7046 |

One settings option adds bit 4 to both writes: 0x4016 then 0x4056, or 0x7016 then
0x7056. Another called transmit path explicitly writes 0x4006 then 0x4046 or 0x7006
then 0x7046. The shared normal entry does not choose a bit-7-set register 0x30 value
for DMR. This is direct MCU register-write evidence, not an inference from a muted
receiver or undecodable transmission. The bit-13-set narrow value 0x6046 and the
0x60C6 digital value differ from this stock transmit entry.

The analog-only part of that entry additionally writes register 0x58 = 0x8405 and
register 0x40 = 0x0030 before applying bandwidth-dependent settings. The digital
branch skips those two writes. Both use the common transmit register 0x30 helper.

The stock packet serializer establishes the exact payload lengths independently
of the buffers passed by command helpers:

| Field | ID | Payload bytes |
| --- | --- | --- |
| Vocoder selection | 0x10 | 1 |
| Work mode | 0x18 | 3 |
| Process mode | 0x1A | 1 |
| Analog sub-audio | 0x26 | 2 |
| Analog configuration | 0x3C | 11 |
| Digital microphone/speaker gains | 0x68 / 0x69 | 2 each |
| Call start | 0x78 | **0** |

In particular, a pointer to a zero-filled buffer supplied alongside call start
does not put zero bytes into that field: the emitted field is its ID alone. The
earlier audio-audit wording about a zero-filled call-start payload must not be
interpreted as a requirement to append any payload bytes.

**Process-mode conditions.** The channel-change configuration distinguishes pure analog, pure
digital and the two mixed channel types recorded in [codeplug-stock-anytone.md](codeplug-stock-anytone.md).
Pure analog selects operating mode 1. Pure digital and mixed channels that transmit digital
select operating mode 4; mixed channels that transmit analog select mode 3. These paths select
vocoder 16. With vocoder 16, the process-mode chooser sends 0x82 when the operating mode is
greater than 2 and 0x80 otherwise. Consequently, observing 0x82 elsewhere in the image is
not evidence that a pure analog channel needs 0x82: its recorded selection is 0x80.

**Microphone timing.** Called transmit-control paths enable the left microphone by writing
page 1 register 0x34 = 0x80 followed by the configured left PGA gain before advancing into
transmit. Release paths disable it with register 0x34 = 0x00. Reapplying these documented
settings after analog vocoder selection would match the stock ordering more closely than
only applying them before subsequent vocoder changes. This does not establish that a vocoder
change resets the codec: the MCU image cannot reveal that resident-baseband behavior.
No additional called line-driver power, ADC power or DAC mute operation was established.

### TX path corrections from the stock audit

The board now supplies its register 0x30 control profile to the shared AT1846S driver: narrow idle 0x4006,
RX 0x4026 and both TX modes 0x4046; wide idle 0x7006, RX 0x7026 and both TX modes 0x7046. The old shared
narrow TX 0x6046 and DMR TX 0x60C6 were not the observed values for this RF path. FM TX also applies
register 0x58 = 0x8405 and 0x40 = 0x0030; release reapplies the saved receive profile. Other boards retain
the default control table. These are code corrections to observed register sequences, not an on-air result.

The audio profile's first two codec writes are IN1L routing followed by unmuted left MICPGA gain. Analog
TX now reapplies and acknowledges them after vocoder loading, before TX work mode and call start. DMR
start uses the existing codec-start list for both writes, and its per-TX codec hook reapplies IN1L routing.
This matches the observed microphone-enable ordering more closely; a vocoder-induced codec reset is not
established. The placeholder gain is still not factory calibration. Pure FM retains PROCESS_MODE 0x80 and
bare-ID call start, both confirmed by the stock audit. No new ADC-power or microphone-bias values are guessed.

`board.fm.mic_writes` reports how many TX microphone-prepare codec writes have been acknowledged during
the latest press (two for this profile). It does not measure microphone samples. A rejected write leaves
analog error nonzero and prevents the modem call from starting.

### Stock calibration copy markers and NAND remapping, 2026-11-16

This audit corrects the earlier copy-selection interpretation. The calibration-save path first
erases the inactive copy and writes its calibration data. It then writes bytes 0x55, 0x55,
0xAA, 0xAA at the new copy's offset 0x1FFFC. Finally, it writes 0xAA, 0xAA, 0x55, 0x55 at
the old copy's offset 0x1FFF8. A retirement marker must not be treated as evidence that the
copy is current. The active predicate and loader selection are described in section 5.
Neither record is ASCII or BCD encoded on the demonstrated DAC2 path: its first two bytes
are copied unchanged into RAM and read as a little-endian unsigned halfword.

**Logical NAND addresses.** Normal stock NAND reads apply an optional persisted block map
before issuing the SPI-NAND page-read command. The erase-block size is 0x20000 bytes. Each
entry replaces a logical block number while preserving the low 17 address bits. A logical
address with no matching entry passes through unchanged. The calibration blocks are logical
block 0x017B for 0x02F60000 and 0x017C for 0x02F80000. Replacement blocks are allocated from
physical byte addresses 0x03000000 through 0x03FFFFFF, block numbers 0x0180 through 0x01FF.
Thus a raw physical read at a calibration logical address need not return its current data.

**Map log format.** The map resides in physical erase block 0. It uses 1024-byte log slots,
starting at byte address 0, with at most 125 slots examined by the loader. Each stored record
is 0x1F8 bytes:

| Record offset | Format |
| --- | --- |
| 0x0000 through 0x00F9 | 125 little-endian unsigned 16-bit source block-number slots |
| 0x00FA through 0x01F3 | 125 little-endian unsigned 16-bit replacement block-number slots, paired by index |
| 0x01F4 | 0x55 marks a written log slot |
| 0x01F5 | log-slot index written by the stock map writer |
| 0x01F6 | number of mapping entries |
| 0x01F7 | runtime map-enable flag, written as zero in the persisted record |

The loader disables mapping while reading this log. It scans slot marker bytes at
0x1F4 + slot * 0x400 until the first marker other than 0x55, or until 125 slots. It loads
the last consecutive marked slot; with no marked slot it still reads slot 0. A separate
25-byte identification marker at physical 0x1FF00 gates normal map loading. Its standard
IEEE CRC-32 is 0xD0D91952 (the usual reflected algorithm, initial/final XOR 0xFFFFFFFF);
its SHA-256 is `adff2a2cb2e1c80e15b1f500df243f3cbffd49c30e6ad0e74fff254799020545`.
These fingerprints cover exactly 25 bytes. Once the
identification marker is accepted, stock loads the map and enables remapping. The observed
startup path refuses a count of 90 or more. No checksum or per-entry range validation was
identified in that load path; a new read-only parser should validate its bounds independently.

If the identification marker is absent, stock enters a raw block-scan and map-reconstruction
path that can erase and write flash. It is not a safe read-only fallback to reproduce during
bring-up. A port must report an unavailable map distinctly from an identity map and must not
rebuild or write the owner's calibration. Normal reads with mapping enabled use the resolver;
an internal bypass flag suppresses it for raw operations. ECC remains the SPI-NAND device's
page-read behavior; the observed address transformation is block replacement, not stripping
ASCII fields, changing byte order or interleaving ECC bytes into the calibration record.

### Calibration reader repair after receive regression

The owner reports FM transmit still has no speech and DMR no longer works in either direction with the
combined TX-path image. Live diagnostics show count 1900, factory 0, mic_writes 2, zero analog errors and
zero decoded DMR receive calls. A receive-only channel switch brings DMR active with digital readiness,
so modem startup completes. The radio's volume read zero during this check; that does not explain the
absence of decoded calls. The agent did not transmit.

The next reader uses the corrected active/retired markers and the persisted NAND replacement log through
[the shared read-only resolver](nand-remap.md). It checks the 25-byte identification fingerprint before
loading the log and refuses an absent or malformed map. A calibration copy must satisfy the active predicate to be
eligible; the primary is preferred. This is stricter than the observed stock blind mirror fallback.
Neither map reconstruction nor calibration writes are provided. `control_dac.map_entries=-1` means the
map is unavailable, zero means a validated identity map and a positive count means replacements loaded.
A missing calibration leaves the control DAC uninitialized; count 0 with factory 0 is not an applied zero.
No receive-recovery or transmit-speech success is claimed before hardware verification.

Map diagnostics: `control_dac.map_status` is 0 before a load, 1 for identification-read failure, 2 for a
fingerprint mismatch, 3 for an invalid/unreadable map log and 4 for a loaded map. `.map_crc` is the CRC-32 of
the 25 identification bytes when readable; it is not a calibration dump or a frequency measurement.

### Stock common transmit gain trims, 2026-11-16

After selecting the transmit bandwidth, the normal transmit entry applies common gain trims
before the register 0x30 transmit-enable writes. There is no band selection inside this trim
helper. It serves both analog and digital transmit. The order is:

1. Set register 0x0A bits 14 through 11 to binary 0111, preserving the remaining bits.
2. Read register 0x0A, retain mask 0x780F, then set 0x03A0. Together these operations leave
   0x3BA0 plus the previous low nibble.
3. Write register 0x44 from the table below for pure analog channels; other channel types
   receive 0x0BFF.
4. Write register 0x41 = 0x070F.
5. Write register 0x59 = 0x0C50.

The pure-analog transmit gain selector is the settings-record byte at offset 0x43. Its own
ROM table contains five bytes, 0x07, 0x08, 0x09, 0x0A and 0x0C. The selected byte is shifted
into register 0x44's high byte; its low byte is 0xFF. This is independently verified from
the transmit helper, not inferred from the receive table. Settings reset selects index 2;
settings loading replaces any index greater than 4 with index 2.

| Index | Transmit register 0x44 |
| --- | --- |
| 0 | 0x07FF |
| 1 | 0x08FF |
| 2, reset default | 0x09FF |
| 3 | 0x0AFF |
| 4 | 0x0CFF |

These transmit values must not be confused with the receive trims in section 4.6.4.
Whether correcting them restores intelligible microphone modulation remains a hardware
question; this audit establishes the register writes and their order only.

### Applying the complete TX trim

The board now applies the stock-audited common TX trim after bandwidth setup and before RF keying: register
0x0A bits 14:11 become 0111, followed by keep-mask 0x780F/set 0x03A0; register 0x44 becomes 0x09FF in FM
(the verified reset index 2) or 0x0BFF in DMR; register 0x41 becomes 0x070F and register 0x59 becomes 0x0C50.
The old path had retained receive gains and the shared driver's voice/deviation values during transmit.
These writes are in addition to the FM TX filter. This uses the documented FM TX default, not a decoded saved
index; the audio gains still need per-radio calibration support.

Before applying the temporary trim, the board saves the initialized receive values of 0x0A, 0x41 and 0x59.
TX release and direct RX entry restore them, and the RX mode helper restores 0x44 and the receive filters.
Otherwise a cached mode table could leave TX gain values behind in RX. Host tests check narrow/wide TX,
DMR TX, register values and both receive-return paths. Hardware speech remains unverified.

### Latest owner audio observation, 2026-11-16

A repeat test reveals FM microphone speech at a very low level. Earlier "no speech" entries describe the
owner's initial observation, not proof that the microphone produced zero signal. A subsequent live read
confirms the owner has installed the complete-TX-trim, map-aware image: map status 4, three entries and
identification CRC 0xD0D91952, but factory 0 and control count 0. The saved DAC word is still unavailable,
so no calibrated carrier alignment is established. The owner reports "TS1 late" on DMR receive and no
recognized TX call at the peer. Neither observation establishes a measured carrier offset, calibrated
deviation or successful complete DMR call.

### Calibration selection edge cases and codec gain bytes, 2026-11-16

The explicit stock calibration-reset path erases both logical calibration blocks, writes
its defaults into the primary, then writes the primary's new-copy marker at 0x1FFFC.
Normal calibration save erases the inactive copy, writes its data, marks it new, and then
retires the previous copy. Losing power after marking the new copy but before retiring
the old copy can leave both copies active; the stock loader still prefers the primary.
Neither copy being active is not the ordinary outcome of that final save window.

The stock loader's unconditional mirror fallback is established behavior, not proof that
a mirror with unrecognized markers is intact. When a read-only port rejects both copies,
record each resolved physical base, whether each read succeeded, both eight-byte marker
areas and both raw first words before changing the selection rule. Do not apply a nominal
DAC value or erase/rebuild the map to resolve missing evidence.

The codec startup gains come from the first 39-byte parameter block at logical calibration
offset 0x0080. Its RAM destination is 0x2000A08B; the startup gain pointer is 0x2000A09D,
18 bytes into that block. Thus the logical NAND offsets are:

| Logical calibration offset | Use | Stock calibration-reset default |
| --- | --- | --- |
| 0x0092 | Codec page 1 register 0x12, LOL driver gain | 0x04 |
| 0x0093 | Codec page 1 register 0x13, LOR driver gain | 0x08 |
| 0x0094 | Codec page 1 register 0x3C, right MICPGA volume | 0x0C |

Each is an individual byte passed through unchanged. These addresses use the selected
logical calibration copy and the stock block-remapping resolver. The reset values are
not measurements of this owner's saved calibration. Left microphone gain follows its
own settings-to-gain lookup; it must not be read by extending this three-byte list.

### Left microphone, HPL and digital microphone gain sources, 2026-11-16

The left MICPGA gain is a user setting, not the next byte after the three factory codec
gains above. Byte 0x0F of the settings record at RAM 0x20008134 indexes a five-entry ROM
map. Indices 0 through 4 produce codec gain bytes 0x18, 0x1C, 0x20, 0x24 and 0x2A. The
selected byte becomes page 1 register 0x3B when microphone routing is enabled. This audit
has not established its reset index or an index-validation rule. In particular, calibration
bytes 0x0095 and 0x0096 must not be assumed to contain this left MICPGA gain.

HPL gain is derived from the volume knob and the selected volume curve described in
section 4.6.2. It is not a direct factory calibration byte. The enabled HPL operation masks
the requested byte with 0x3F before writing page 1 register 0x10, clearing mute bit 6.
The disabled operation writes 0x40. Negative gains use the codec's six-bit signed encoding.

Digital microphone gain, field 0x68, normally takes two bytes from logical calibration
offsets 0x009B and 0x009C. The packet value is MSB first, unlike the little-endian DAC2
word at offset 0. The calibration-reset default is bytes 0x07, 0xD0, representing 2000.
When settings-record byte 0x57 at RAM 0x20008134 equals 1, the setup path instead sends
0x03, 0xE8, representing 1000. The packet field has two payload bytes in either case.

The observed codec-bypass wrappers for LOL gain, LOR gain, right MICPGA gain and left
MICPGA gain pass their eight-bit argument unchanged. They do not mask mute or sign bits
and do not clamp gain. This differs from the enabled HPL wrapper's explicit 0x3F mask.
The known left microphone lookup produces bytes with the PGA mute bit clear. These facts
describe MCU command construction; they do not constitute codec register readback.

### Native microphone-gain hook and calibration read windows

The previous board had no `hal_radio_t.mic_gain` hook. The existing UI setting therefore could not change the
native SCT microphone path. The board now stores its -10..+10 dB offset and prepares the left MICPGA value at
the next analog or DMR transmission. The current baseline is 0x20 (16 dB); a dB adds two codec counts, clamped to
0..95 with mute clear. DMR's pre-TX callback refreshes the profile and its codec-TX hook applies PGA gain;
IN1L routing remains in the channel-start codec sequence. FM reapplies route and gain with acknowledgments.
`mic_gain.offset_db` and `mic_gain.requested_pga` report requested settings, not codec register readback.

The input PGA and AGC maximum gain use half-dB gain units, but page 0 register 0x56 contains AGC target and
hysteresis controls. The dB offset now preserves that control byte at its baseline 0xA0 while adjusting the
PGA and maximum gain. Previously +10 dB changed the control byte from 0xA0 to 0xB4: the target fell from
-10 dBFS to -12 dBFS and hysteresis changed. This partly opposed the requested gain increase. These bit
meanings come from [TI's application reference, section 5.2.81](https://www.ti.com/lit/ml/slaa557/slaa557.pdf).
This correction is host-tested; its effect on the owner's weak transmit speech remains unverified.

With CONFIG_DIAG, `cal.read_windows=1` advertises the calibration inspection interface. CHIP_REG's byte reads
expose chip 3 only: page 0/1 selects the primary/mirror record's first
256 bytes, page 2/3 selects its eight tail-marker bytes. Register is the byte offset in that window, with at
most 32 bytes per request. Requests crossing a window or made while keyed are refused. Reads follow the saved
NAND map; no write interface, fallback copy selection or DAC change is added by these diagnostics.

A read of the installed image's own SRAM map showed replacements 3->384, 1536->385 and 1537->386. Neither
calibration block is relocated on this radio, and the power-calibration-loaded flag was zero. The next read
must inspect both marker areas and raw first words; strict copy selection is not relaxed without evidence.

Calibration inspection pages 4..7 mirror windows 0..3 using the NAND diagnostic reader. These replies
prefix the bytes with an ECC-failure flag (0 means none reported, 1 means at least one page failed ECC).
The requested length includes that flag. At most 31 data bytes follow it; for a DAC word request 3 bytes,
for a marker area request 9. Raw diagnostic data is never applied as calibration by this interface.

### Zero-volume mute correction

The owner reports loud receive noise with the harness removed, including after volume override zero. Live
state showed FM RX opening at about -127 dBm, monitor off, and volume.sent=0. The codec volume callback had
mapped zero to -6 dB with mute clear, while the core's receive gate remained allowed to open. Zero now sets
HPL mute and holds both speaker-route and amplifier pins low regardless of the requested receive gate.
Restoring nonzero volume reapplies the requested gate. This fixes mute behavior; the cause of loud noise at
nonzero volume and weak-signal squelch openings remains unverified. The USB link disappeared before a
receiver-only channel change could be completed.

### Missing mode-entry controls, follow-up to the session handoff

The board now supplies SQ_LEVEL (0x6C) at digital and analog setup: 00 00 00 3C followed by big-endian limits
0F3C 0A8C 0F3C 0A8C, with the last two limits FFFF in analog mode. MISC_GAIN (0x6B) uses a 12-byte payload:
08 00 08 00, calibration bytes +0x4A..+0x4D, 00, calibration +0x4E, then two reserved zero bytes. The calibration
reader retains reset values 80 00 80 00 00 when that record is unavailable or erased.

The stock startup API updates ten bytes but its emitter sends twelve from persistent storage. The final two
stock bytes are not established by that producer. Reserved zeros are a public-manual choice, not recovered
stock values. The initial ANALOG_CONFIG used nominal gains; its calibration source is now established below.
The new controls are mode-entry data; call-start/stop signaling is retained. Hardware audio is unverified.

### Tuning-output timer correction

PA6 uses GD32 TIMER2 channel 0 at 0x40000400. The platform's native `TIMER_BASE(3)` instead selects TIMER3 at
0x40000800, while the MCU abstraction's `mcu_tim_clock_on(3)` enables TIMER2. The board had mixed those two
numbering conventions. It now uses `MCU_TIM(3)` with that clock call. The mapping is defined by the
[GigaDevice datasheet](https://www.gd32mcu.com/data/documents/datasheet/GD32F303xx%20Datasheet_Rev3.3.pdf)
and [timer memory map](https://www.gd32mcu.com/data/documents/userManual/GD32F30x_User_Manual_Rev2.9.pdf).

The calibrated duty is applied at RX tune, TX tune and restoration of the saved RX frequency after TX.
Previously only TX called the tracking helper. Diagnostics now expose `board.tuning.pwm_ready`, the actual
`board.tuning.pwm_compare` register and `board.tuning.pwm_expected` from RX calibration. Expected 65535 means
calibration is unavailable. Ready checks clock, counter enable and output-channel enable; it does not measure
PA6 voltage or radiated power. The keys precede the codec snapshot to fit the debug reply's size limit.

The host RF test catches both the mismatched clock/register block and omitted RX/restoration updates.
On the owner's `8d1b53bc` image, FM receive at 433.500 MHz reports PWM ready=1 and actual compare=1972,
equal to the calibrated expected value. FM setup is ready with no errors or timeouts. This establishes the
timer register state; PA6 voltage and RF output were not measured.
The hardware power, carrier alignment and speech outcome remain unverified on the corrected image.

### Stock DAC modes and remaining RF setup differences

The stock MCU setup writes each DAC channel's control field as 0x0002 before enabling it. Output buffers,
triggers, waveform generation and DMA are off; enabling gives 0x0003 per channel. PA4 power writes remain
12-bit right-aligned with the calibration byte shifted left four. The generic platform initializer had left
output buffering enabled. This board now establishes both unbuffered channel modes before enabling PA4;
PA5 remains disabled until its calibration is available. The saved power and frequency counts are unchanged.
Host tests check both channel fields and clearing of stale mode bits. RF and voltage effects are unverified.

The audit also found stock RF boot values 0x54 = 0x2A18, 0x5A = 0x0CDD and 0x63 = 0x3FFF, differing from the
shared driver's 0x2A3C, 0x06DB and 0x16AD. Those differences and the extra calibration sequences are not yet
applied. The alternate 0x0F = 0x8C24 / 0x13 = 0xB100 path is conditional on saved five-byte metadata matching
ASCII `1846N`, read from logical NAND 0x02FA0120 or the selected mirror at 0x02FC0120. It is not established
as RF-chip autodetection. No extra GPIO change was justified: GPIO1 remains unchanged in the traced normal
TX/RX-return path, and GPIO3 is high in ordinary TX. Live register 0x79 = 0x1932 and 0x7A = 0x204B already
have the stock source helper's cleared high bits, so those bits do not establish a missing routing change.

### Calibrated modulation gain in FM mode setup

The reachable stock mode resend emits store id 0x10 as `MOD_GAIN` (field 0x2C), without an analog/digital
predicate. FM entry marks this field dirty. Its source is the selected band's big-endian calibration
halfword at +0x0090 for UHF or +0x00D0 for VHF, copied to store offset +0x34 and sent unchanged.
This is a reachable mode-setup field, unlike the unused library transmit helper.

The port previously updated its gain buffer before TX but sent it only through the digital engine's TX
fields. FM mode setup now sends the same calibrated buffer before RX work mode. Frequency selection updates
the buffer under the modem lock; a changed gain restarts analog setup, including on return from a split TX.
The FM-only build uses this path too. The profile test fails when receive work mode precedes modulation
gain and passes with the field present. This corrects an omission; restored speech remains unverified.

The ordinary microphone setting 0x20 produces codec writes 1:34=80, 1:3B=20, 0:56=A0 and 0:58=20, each
acknowledged separately. Their completion does not establish ADC power, bias or sample routing. Pure FM
uses processing mode 0x80; mixed/digital paths using 0x82 do not justify changing the pure-FM value.

### Recovered FM analog gain calibration

The earlier conclusion that ANALOG_CONFIG's ten gain bytes were unavailable was incorrect. The reachable
boot audio producer copies the first fifteen bytes from calibration +0x0040. Audio store id 0x15 takes the
first ten, and its setter copies them to the RAM buffer used for field 0x3C bytes 1–10. Pure FM sends those
bytes unchanged. The addition of 40 to the final big-endian word tests raw channel type >=2, covering additional
hybrid behavior whose full UI enumeration is unresolved. Pure FM type 0 and pure DMR type 1 do not take it.
The earlier description of that branch as generically digital was incorrect. The radio-store emitter sends
ANALOG_CONFIG only when the vocoder store is 16; this producer does not require it in ordinary DMR.
The five remaining bytes are the MISC_GAIN calibration already used by this board.

Both saved calibration copies on the owner's radio match these factory defaults:

| Stage | Calibration word | Linear gain format | Previously sent |
| --- | --- | --- | --- |
| TX before limiter | 0x4650 (18000) | Q16.11 | 0x0800 (unity) |
| TX after limiter | 0x0578 (1400) | Q16.12 | 0x1000 (unity) |
| CTCSS | 0x09C4 (2500) | Q16.12 | 0x1000 |
| DCS | 0x05DC (1500) | Q16.12 | 0x1000 |
| RX volume | 0x010E (270) | Q16.11 | 0x0800 (unity) |

The port now reads calibration +0x0040..+0x0049 through the selected, remapped calibration copy. An unreadable
or wholly erased field uses these factory defaults; zero gain bytes are preserved. Mode byte 0 starts at
0x80, matching the stock emitter's forced bit 7. The shared engine selects narrow/wide bits without replacing
the board gains. Packet, primary/mirror selection, relocated-block and failed-read tests cover this path.

`board.fm.analog_requested` reports the board template and `board.fm.analog_config` the latest acknowledged
eleven-byte field, as hexadecimal strings. The latter is `-` until acknowledged. These are baseband command
diagnostics, not codec readback. The receive-only HIL check compares their gains and channel-width bits.
Microphone PGA and saved calibration are unchanged. Hardware confirmation of transmit speech is pending;
the restored RX gain is lower than the former unity setting, so receive volume can also change.

### Whole DMR and microphone path audit

This audit compares the reachable stock MCU lifecycle with the shared engine and board profile at `b17ef9ee`,
before the microphone correction described below.
The resident modem's internal codec/ADC and vocoder implementation remains outside the traced MCU path.
An accepted packet or recognized call header does not measure microphone samples or intelligible voice.
The shared protocol sequence is described in [modem-sct.md](modem-sct.md).

| Stage | Current RadioFoundry path | Audit result |
| --- | --- | --- |
| Boot and common audio | Boot block, vocoder 4, calibrated DEMOD_GAIN and digital mic/speaker gains, then codec input/output trims | Sources match the identified calibration. Explicit TI CODEC_SELECT is a proven stock startup step; additional MCU ADC/MICBIAS setup is not established |
| Return from FM | Drain analog ownership and select vocoder 4 before digital readiness | Analog gain calibration belongs to the analog engine; do not copy field 0x3C into ordinary DMR on the strength of the +40 branch |
| Digital start | Processing mode 2, report fields, channel CC/own ID/call options, board start fields, RX work mode and slot selection; microphone start writes and band MOD_GAIN follow | Shared layer handles signaling; a board provides wiring and calibration. A call recognized through dummy-load leakage is not full validation of this path |
| Receive | RF band enables and tuned frequency, CARRIER_READY, sync/slot/LC reports, native modem voice playback and HPL/speaker gating | The codec's receive side working does not prove microphone capture. Slot timing and codec delay remain separate RF measurements |
| RF TX preparation | Saved reference DAC and PA6 tracking, RF microphone-source select and TX trims, band selection, PA power and PD0 slot-edge gating | Readback verifies programmed values, not modulation quality, reference voltage or RF power |
| Microphone setting | Gain offset updates the left PGA row and AGC maximum row in memory | Both must reach the codec together; AGC control contains target/hysteresis fields and must not be treated as another linear gain |
| Microphone at digital TX | The board's `codec_tx` points only to the left-PGA row; the four-write profile is sent only by `codec_start` | Confirmed mismatch: a setting changed after start can raise PGA while the old AGC maximum remains in force |
| Voice generation and signaling | Shared engine starts native voice from LC metadata and paces later voice bursts at 60 ms; the resident modem supplies encoded microphone voice | Header recognition does not establish valid encoded speech or sufficient RF decode margin |
| Codec acknowledgment | Shared digital engine discards codec-type packets, including errors; its single TX codec write does not wait for acceptance | An engine-level TX success cannot establish mic-write success. Stock codec wrappers use separate acknowledged transactions |
| Release and return | End/drain call, gate PA, restore receive RF frequency/bandwidth/band enables and carrier handling | No newly established mandatory microphone-reset or converter-reset write was found for every ordinary PTT |

Stock's ordinary Mic setting selects gain bytes 0x18/0x1C/0x20/0x24/0x2A and updates two audio-store
operations: route/PGA and AGC control/maximum. Each codec write uses a separate TYPE 0x40 acknowledgment
(200 ms in the traced wrappers). A cached runtime helper can reconnect IN1L and reapply PGA. Its other
selector controls HPL; those speaker calls must not be counted as microphone resets.

For example, this board's Mic offset +10 dB changes requested PGA and AGC maximum from 32 to 52, but after
digital start only the PGA reaches the codec. The old ceiling can constrain the increase. Mode entry also
calls the gain-field hook after `codec_start`, so in-memory freshness must be considered there. This explains
a gain-setting consistency bug, not necessarily quiet audio at the unchanged baseline setting.

The digital microphone correction must apply the current coupled settings with acknowledgments before
TX work mode. Replaying all four current microphone writes per call is one implementation strategy, not a
claim that stock performs four writes on every PTT. Shared sequencing must preserve the existing mobile
board's codec mute/restore behavior. Rejection, timeout and release during preparation need host tests.
That correction is not implemented in the earlier FM image `fc1c9900`; the newer candidate includes it.

The stock resend also carries a twelve-byte DC_OFFSET block. No new nondefault value for it, ordinary-voice
VOCOER_IO_SET transaction or additional ADC power/bias/routing write was established by
this audit. Service/tone full-duplex commands and special stop debug bits are not ordinary voice defaults.
Raw codec-I2C address convention, resident mode-change effects, actual ADC samples, deviation and burst
timing remain unresolved. Normal-antenna DMR voice still needs testing independently of microphone gain.

### Acknowledged runtime microphone setup

The board now gives both FM and DMR the complete current four-write microphone profile. DMR no longer
uses a PGA-only TX write or unacknowledged microphone start writes. Its gain-field hook updates the PGA
and AGC maximum before the runtime owner snapshots the batch; the AGC target/hysteresis byte stays fixed.
The shared helper serializes microphone and HPL writes, and the modem task routes codec ACKs to it first.
An accepted volume update is queued, not proof that the codec applied it.

DMR slot gating is armed with the next microphone batch generation. PA output starts off even if PD0 is
already high. The slot ISR requires that generation to be fully acknowledged before following PD0. It
does not take a mutex or make an RTOS call. A prior call's completed setup cannot authorize a fresh call.
Stock's four writes per ordinary PTT remain unproven; this is the port's explicit fresh-state strategy.

`board.codec_runtime` exposes `acked`, `error`, `fault`, `mic_pga`, `mic_ceiling`, `timeouts` and `rejected`.
The last acknowledged PGA and ceiling can be compared after an owner-operated call without keying RF.
Errors 0xFE and 0xFF identify send failure and timeout. After either, restart the radio to reset the fenced
codec stream. An explicit codec NAK is reported and prevents TX; its ACK has drained, so a later call can retry.
These keys describe accepted commands, not ADC samples or a measurement of intelligible voice.

Host tests cover coupled gain updates, volume ACK ordering, each mic-write rejection, timeout, late ACK,
early release, abort, stop, mode restart, data queued before preparation and fresh-call PA gating. The
other SCT board retains its legacy mute/restore path. Hardware speech results remain pending.

### Physical microphone path: evidence boundary

The owner still reports white noise and quiet FM speech on `4dff8084`. Live diagnostics confirm four
acknowledged mic writes, PGA=ceiling=32, no codec errors/faults/timeouts, FM ready, and analog configuration
`804650057809c405dc010e` acknowledged. Accepted commands do not establish intelligible microphone capture.

The fresh isolated stock audit confirms codec-bypass encoding and the Mic-setting route/PGA/AGC command
chain. The traced accessory flag selects speaker gain/volume behavior; no microphone mux or bias GPIO was
established. Capsule-to-IN1L continuity, actual bias/ADC mute, I2S microphone samples, and modulation-output
continuity remain unproven. Earlier wording implying that entire physical chain was confirmed was too strong.
The resident modem application's converter configuration is not established by the MCU trace.

The subsequent full startup trace established a missing `CODEC_SELECT = 1`; this is restored in
`8a566a6a`. A re-audit of the MCU codec-write builders found no additional normal ADC, bias, digital-mute,
clock or input-routing writes. The remaining initialization table and mode-dependent sample routing are
inside the undecoded resident BA payload. The trace does not establish whether codec selection resets or
reinitializes those registers. No further register patch is supported by this evidence.

Continue firmware-only analysis of that resident initialization when possible. Codec readback or passive
I2C/I2S capture would provide independent evidence, but the owner has no scope or logic analyzer; capture
is not a prerequisite for further firmware analysis. The supplied owner folder has no schematic; public
board photos do not establish capsule continuity. The new image has not been flashed or speech-tested.


### Resident baseband extraction checkpoint

The offline dirty audit reconstructed all six supplied BA flash streams and validated every byte against
the source container: 1,177 outer checksummed records, 192 internal load records, 294,472 supplied bytes.
The internal records carry a marker, destination and word count. The updater independently constructs
these headers from instruction/data memory metadata and an explicit coding selector. All BA records are
coded: 179 instruction records and 13 data records. The host forwards already coded payloads unchanged.
The same extraction method validates three earlier supplied versions.

| Flash start | Role established by the updater's trailer metadata |
| --- | --- |
| 0x000100 | User application, instruction entry 0x0800 |
| 0x018000 | Vocoder 4 (AMBE+2) |
| 0x02C000 | Vocoder 12 |
| 0x040000 | Vocoder 16 (Analog) |
| 0x051000 | Vocoder 23; further meaning not established |
| 0x068000 | Second user application, instruction entry 0x0800 |

Vocoder-4 and vocoder-12 streams are byte-identical across all four supplied versions. The analog stream
and both applications change. These roles do not identify which application owns the codec or prove that
both applications run together. The supplied installer contains small uncoded download examples, but no
uncoded resident equivalent or TI3204.txt initialization script was found.

Extraction is complete for the supplied records; decoding remains unresolved. No validated instruction
decoder or payload transformation was recovered. A particular cipher, compression scheme or ROM decoder
location has not been established. The resident TI initialization table and FM/DMR microphone routing
remain unrecovered. This audit supports no additional microphone register patch. Extracted payloads and
reproduction tools remain in ignored storage in the dirty tree; only prose crosses the clean-room boundary.


### Confirmed default microphone setting

A follow-up trace reaches the settings-reset initializer: it stores microphone index 2. The ordinary
producer maps indexes 0 through 4 to PGA bytes 0x18, 0x1C, 0x20, 0x24 and 0x2A. The current route 0x80,
PGA 0x20, AGC control 0xA0 and ceiling 0x20 therefore match the initialized middle stock setting.
No higher default gain is justified by this trace.

Microphone enable writes page 1 register 0x34 = 0x80, then register 0x3B = the selected gain. Disable
writes register 0x34 = 0 only. Nonzero AGC control writes page 0 register 0x56 = control OR 0x80 and
register 0x58 = current PGA gain. Zero AGC control writes 0x56 = 0 and leaves the ceiling unchanged.
These producers have no established FM/DMR or accessory-dependent alternate input selection.

Normal startup flushes queued audio settings after vocoder selections 4 then 16, then radio settings
select the stored vocoder mode. The ordering is established; an ADC initialization side effect of
selecting 16 is not. The audit supports no additional microphone register correction beyond codec
selection. Codec state readback is needed to distinguish accepted gain commands from actual ADC setup.
The owner still reports weak mic audio and overwhelming static at the peer after codec selection.


### Recovered modulation-output startup field

The normal final startup stage sends `MODOUT_CONFIG` (0x41) after the main vocoder-selection stage and
before the queued audio/radio settings. The wrapper uses the runtime protocol byte, initialized to 5;
the wire TYPE is therefore 0x05. Its five data bytes are `00 1D 00 00 00`. Byte 0 is the output-delay
store, initialized to zero; the wrapper sets byte 1 to 0x1D and clears bytes 2 through 4. No meaning beyond
the public delay-byte definition is assigned to the reserved bytes.

This supersedes the earlier claim that no reachable modulation-output transaction was established.
The port omitted this field entirely. Add the exact typed field to startup after the configured vocoder
and before gain/control and codec-register writes, waiting for its acknowledgment. A refusal must stop
voice setup. This restores a known command; the current profile does not reproduce the complete stock
vocoder preload sequence, and neither retention across vocoder changes nor a mic-level repair is proven.
Ordinary mode-entry emitters are not established callers of this startup wrapper; do not add speculative
per-PTT or mode-change output writes.


### Runtime vocoder selection qualification

A subsequent reachable stock trace distinguishes stored vocoder selection from the emitted value.
Ordinary initialization stores protocol/mode 3 and requested vocoder 4. When the mode is at least 2 and
requested vocoder is not 16, the first-send and retry helpers emit TYPE 0 `VOCODER_SEL = 12`, wait for
field-0x10 status zero, and cache the requested value. A request for 16 emits 16. This qualifies earlier
wording that the radio-field flush simply sends stored selection 4 again.

The extracted index-12 stream exists, but its vendor-specific role is undecoded. The current layer-2
engine selects 4 directly; the stock runtime selection is a wire difference, not proof that substituting
12 in the layer-2 engine repairs microphone audio. No such substitution is implemented.

The mode-entry and vocoder completion paths do not establish a full codec/audio-cache invalidation
caused solely by switching vocoders. The trace does not justify resending every codec trim after each
switch. The verified modulation-output startup command is the only new setup correction from this pass.
