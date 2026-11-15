<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AnyTone AT-D878UVII PLUS hardware

Status: facts only. Sections 2 to 6 are read from the stock image, so **nothing in them is confirmed on a radio:
read every line as (unverified)**; section 1.1 is read off a board in photographs. The family page is
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
wider UHF tables.

## 1. The radio and its image

### 1.1 Chips

Read off the board in the public internal photos of FCC ID T4KD878UVII (an AT-D878UVII PLUS). The same filing's
schematic, block diagram, operational description and parts list are withheld under long-term confidentiality.

| Part | Marking | Notes |
| --- | --- | --- |
| MCU | `GD32F303VGT6` | Cortex-M4, 1 MB flash, 96 KB RAM, no CCM. 100-pin LQFP |
| DMR baseband | `SCT3258TD` | QFN64. Runs DMR layers 1-2 and the AMBE+2 vocoder itself ([modem-sct.md](modem-sct.md)) |
| Audio codec | `AIC3204` | TLV320AIC3204. Set up through the baseband, not from the MCU ([hardware-anytone.md](hardware-anytone.md) 2.5), which is why it is on no bus in section 4 |
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
| Update mode | PTT + PF3 held at power-on; the red LED blinks. **Confirmed on a radio**, together with a whole write: 931,392 bytes in 29,106 frames, after which the radio installed the image and restarted clean |
| After an update | "MCU reboot": PTT + PF1 at power-on. It erases the codeplug |
| Model string | `D878UV2`. **Confirmed on a radio**: in update mode it answers model `D878UV2`, hardware byte 0, protocol `V100`, which is the GD variant |
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
| PA6 | TIM3 channel 1 PWM, period 4095, AF push-pull: a **frequency-dependent analog control voltage** (VCO or front-end tracking; which is not identified). The duty comes from two 16-bit tables held in RAM. At or below 174 MHz, a 39-entry table on a 1 MHz grid from 136 MHz, interpolated at 100 kHz steps and clamped to its last entry. Above 174 MHz, a 6-entry table on a 1 MHz grid from 220 MHz — which clamps to its last entry for anything past 225 MHz, so the duty is one constant across the whole UHF band. Two other operating modes load a fixed entry instead. The 868 page guesses PA6 or PB0 is a tuning voltage; on this radio it is PA6 |
| PA7 | **Speaker amplifier enable**, active high, left low at boot. Set in three places, cleared on nearly every state change |
| PE0 | A second audio-path switch, driven opposite to PA7 in the receive path. Not identified |
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
written when the RF chip is set up is 0x5AE2 — gpio0 low, gpio1 built-in function, gpio2 low, gpio3 high, gpio4
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
| +0x1FFFC | `55 55 AA AA` | written **before** the data: a write is in progress |
| +0x1FFF8 | `AA AA 55 55` | written **after** the data: the copy is complete |

A copy is taken as good when the bytes at +0x1FFF8 match; otherwise the other copy is used. Mirroring copies the
block page by page in 2048-byte steps and then writes the tags.

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
3. What PE0, PE15, PB11, PA9 and PC13 do, and whether PE4 is the Bluetooth indicator.
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

## Sources

- FCC ID T4KD878UVII (AT-D878UVII PLUS), internal photos: <https://fccid.io/T4KD878UVII>. The schematic, block
  diagram, operational description and parts list in the same filing are withheld under long-term confidentiality
- Sections 2 to 6 are read from the stock image named in 1.2, under the spec side's rules
  ([CLEANROOM.md](../CLEANROOM.md))
- The family's page, [hardware-anytone.md](hardware-anytone.md), and its sources

## Early startup diagnostic

The opt-in `D878UV2_STARTUP_PROBE` build holds PC3 high and sets PE3 high before clock setup. After resetting the
time base at the selected clock rate, it initializes the LCD, enables PB0 and draws the boot splash. It then
blinks PE3 every 500 ms without starting the scheduler or initializing storage, USB or the modem. This is a
bring-up probe, not normal application behavior. The owner observed a blinking receive LED and a visible splash with probe 1. The splash was rotated 180 degrees
with MADCTL 0x68; the board now selects 0xA8 by toggling MX and MY. The corrected orientation awaits confirmation.
Probe 2 continues into normal startup after the early splash and reports board initialization stages with LED
pulse counts, as listed in the bring-up procedure.
See [the bring-up procedure](../testing/d878uv2-bringup.md#early-startup-probe).

Probe 3 reads and validates the retained crash record before the scheduler starts. A valid record is displayed
with the fault summary, LR, CFSR and HFSR; the probe then waits while servicing the watchdog through the early
delay path. Without a valid record it continues like probe 2. Seven pulses followed by a reset were observed
with probe 2; the failure is not yet localized beyond board initialization returning.

### GPS startup dependency

The GPS task must be created only after the RF bus is attached and AT1846S initialization has finished.
The GPS power callback writes the RF chip's GPIO register, so creating the task in board initialization lets
it dereference an uninitialized bus while display delays yield the startup task. The observed crash was
`fault 3 at 20001E50 in gps`, LR `08032739`, CFSR `00020000`, HFSR `40000000`: an invalid instruction reached
through the bus callback. GPS initialization now runs at the end of the radio initialization callback.
The corrected image awaits hardware confirmation.

### Power and input bring-up corrections

A normal boot is confirmed after deferring GPS task creation. The next observed faults were zero battery and
volume readings, an unresponsive channel knob, and only digits 7, 8 and 9 responding. Power initialization was
never called, leaving the shared ADC driver uninitialized. Board initialization now calls the power interface
before peripheral setup. The volume display uses the existing MCU-read knob bar; the ADC supplies its position.

The channel encoder now samples PC8 and PC9 on both edges through EXTI8 and EXTI9. Its decoder uses four
transitions per detent with rest mask `QUAD_REST_11`; the previous code passed a pin value as the mask and reset
the consumer's total on every read. Verify steps per click and direction on hardware.

Matrix settling is 5 microseconds per drive change, rather than the shorter guessed loop. The scan restores the
LCD data outputs before returning, including when it preempts a display write. The keypad map is unchanged and
still requires hardware confirmation. The normal 8-second power-switch guard is restored in the controls image;
the previous diagnostic build deliberately ignored shutdown for about 49 days.

With diagnostics enabled, the board view exposes these raw keys:

| Key | Value |
| --- | --- |
| `board.adc.10`, `.11`, `.12` | ADC counts for PC0, battery PC1 and volume PC2 |
| `board.power_switch` | PA0 level, before debounce |
| `board.knob_ab` | PC8 in bit 0 and PC9 in bit 1 |
| `board.matrix_idle` | Sense-low bits with all rows high, captured by the key scan |
| `board.matrix.0` through `.7` | Sense-low bits for each driven row; PB1 bit 0, PB2 bit 1, PC5 bit 2 |

The owner confirms all keys and the battery bar work with the controls image. The channel knob reports one
channel change per two physical clicks with the four-transition setting. The correct transitions per click
and rest-state mask must be measured; the likely two-transition correction is not yet applied.
