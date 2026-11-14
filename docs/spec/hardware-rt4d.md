<!-- SPDX-License-Identifier: MIT -->
# Radtel RT-4D hardware

This page holds the facts a firmware needs to bring up the Radtel RT-4D handheld (also sold as the iRadio DM-4R): MCU,
memory, pins, the RF chip, the DMR chip and its UART protocol, display, storage, the PC link and the update path. The
survey entry is in [radios.md](radios.md).

Sources, as marked on each fact:

| Mark | Source |
|---|---|
| `(schematic)` | Radtel's schematic "RT4DDLT01" V2.1, 2024-06-04, four sheets (RF, MCU, charger, DMR chip), as published in the OpenRTX external documents repository (<https://github.com/OpenRTX/OpenRTX-external-docs>, `Schematics/RT4D-Schematic.pdf`). A scan; part values are read from it |
| `(pinout)` | The MCU pin table on <https://openrtx.org/#/hardware/rt4d>. It agrees with the schematic on every pin |
| `(datasheet)` | Artery AT32F423 datasheet V2.03 (2025-01-17), reference manual V2.04 (2025-06-03), the AT32F423 SVD from the CMSIS-SVD collection (peripheral addresses), or another chip's data sheet named in the row |
| `(module manual)` | NiceRF's published UART manuals for the DMR818 / DMR818S / DMR858 DMR modules (section 5). They describe the same frame and base command set the RT-4D's DMR chip speaks |
| `(stock 3.16)` | Read from the vendor's V3.16 (2025-05-05) update package: the PC updater and the application image it carries, and the DMR chip firmware file next to it. Spec side only ([CLEANROOM.md](../CLEANROOM.md)); no vendor code or image is in this repository |
| `(measured)` | Found on a radio, with the date |
| `(unverified)` | Not confirmed on a radio, in a datasheet or in the vendor files |

`(measured)` marks a fact found on the owner's radio with RadioFoundry test images.

| Section | Contents |
|---|---|
| 1 | Chips, memory, clocks |
| 2 | Pin map |
| 3 | RF and audio paths, BK4819 control, calibration, data flash map |
| 4 | Display, keys, LEDs, power |
| 5 | The HF6853 DMR chip and its UART protocol |
| 6 | PC link, update and recovery |
| 7 | What a port needs, budget, open questions |

---

## 1. Chips, memory, clocks

| Item | Finding |
|---|---|
| MCU | Artery AT32F423CCT7: Cortex-M4F, up to 150 MHz, LQFP48 (schematic, datasheet) |
| Program flash | 256 KB at 0x08000000, 2 KB sectors (sector erase 13.2 ms typical). A separate 20 KB boot memory holds Artery's ROM bootloader (datasheet) |
| SRAM | 48 KB at 0x20000000, no CCM (datasheet). The stock application's initial stack pointer is 0x2000B510, so it uses about 45 KB (stock 3.16) |
| Vendor bootloader | 0x08000000-0x080027FF (10 KB). Never in an update package (pinout, stock 3.16) |
| Application | 0x08002800-0x0803FFFF (246 KB, 251,904 B). The updater always writes all of it; the V3.16 image fills 0x08002800-0x08039543, the rest is written as 0x00 (stock 3.16) |
| Vector table | At 0x08002800. Interrupts the stock application uses: DMA1 channel 1, ADC, TMR1 overflow / TMR10, TMR2, USART3, USART6, SysTick (stock 3.16) |
| Crystal | None on the MCU sheet: no HEXT or LEXT crystal is drawn on PF0/PF1 or PC14/PC15, which are used as GPIO (schematic) |
| Clocks | The stock application runs the PLL from the internal 48 MHz HICK (datasheet: ±1 % at 25 °C). Its start-up, in order: flash control register (FLASH+0x00) = 0x154; PWC clock on and PWC+0x10 bits 1:0 = 3 (LDO voltage); LICK on, then HICK on, each waited for; CRM_MISC1 bit 12 (HICKDIV) set; CRM_PLLCFG PLLRCS = 0 (HICK), PLL_NS = 96, PLL_MS = 1, PLL_FR = 4; PLL on and waited for; AHB divider code 0, APB2 code 0, APB1 code 4; CRM_MISC2 AUTO_STEP_EN on; system clock select 2 (PLL), waited for; AUTO_STEP_EN off (stock 3.16). Decoded with the reference manual (RM V2.04, CRM and flash chapters): flash 4 wait states with both prefetch buffers on (the setting for 128-150 MHz), LDO 1.3 V, PLL post-divider 16, APB1 HCLK/2, APB2 and AHB undivided, system clock PLL/2. The PLL settings don't give that speed: with HICKDIV set the PLL input is 48 MHz, which breaks the manual's 2-16 MHz limit after the pre-divider and its 500-1000 MHz VCO range (48 x 96 = 4608 MHz); with the 8 MHz HICK/6 input it gives 768 MHz VCO, 48 MHz PLLCLK and 24 MHz system clock. The stock application's own clock calculation (used for its delays and UART baud rates) takes 8 MHz as the PLL input whichever source bit is set, and computes 8 MHz × 96 / (1 × 16) / 2 = 24 MHz. The radio's UARTs work at 115200 with that figure, so the stock system clock is 24 MHz (stock 3.16; not measured). RadioFoundry sets its own clocks, so only the hand-over from the vendor bootloader depends on it |
| Vector table offset | The stock start-up code writes VTOR = 0x08000000 first; the application's table is at 0x08002800, so it must move VTOR later (unverified: where isn't read) |
| Data flash | Winbond W25Q128 footprint, PY25Q128HA fitted, 16 MB SPI NOR on SPI2 (schematic, pinout). HOLD and WP pulled up |
| GB2312 font | The stock application draws Chinese text from a bitmap font in the data flash at 0x14C000 (stock 3.16) |
| DMR chip | Horketech HF6853 (U700), its own SoC with a 26 MHz oscillator (X700), USB, SD and I2C pins, and its own firmware file "FM100B" (section 5) (schematic, stock 3.16) |
| RF chip | Beken BK4819 (schematic) |
| Broadcast FM | Beken BK1080, 2-wire bus on PC13 / PC14, power on PC15 (schematic) |
| Audio amplifier | LN4898 class-AB bridge amplifier (U502), enable "AF MUTE4898" on PA2, 16 Ω 1 W speaker (schematic) |
| Charger | CS5090EA, from the USB-C jack (VBUS only; the data lines are not connected). Its STAT output lights the two-color LED D900 (schematic) |
| Switching regulator | SY8121B (U506) from the battery (schematic) |

## 2. Pin map

All 48 pins (schematic, pinout). "SPI2", "USART3" and so on are the functions the stock application selects, with the
IOMUX number it writes (stock 3.16).

| Pin | Port | Net | Use |
|---|---|---|---|
| 2 | PC13 | SCK1080 | BK1080 clock, bit-banged |
| 3 | PC14 | SDA1080 | BK1080 data, bit-banged |
| 4 | PC15 | FM POWER-EN | BK1080 power |
| 5 | PF0 | KEYOUT3 | Key matrix column 3 (output) |
| 6 | PF1 | KEYIN1 | Key matrix row 1 (input); also the A/B key to ground |
| 10 | PA0 | KEYOUT1 | Key matrix column 1 |
| 11 | PA1 | KEYOUT2 | Key matrix column 2 |
| 12 | PA2 | AF MUTE4898 | Speaker amplifier enable |
| 13 | PA3 | BACKLED-EN | LCD and keypad backlight (transistor Q501). The stock application gives PA3 IOMUX 3 (a timer channel): PWM brightness |
| 14 | PA4 | AUDIO | DAC1 out: tones and beeps into the audio path (through R536 to the "TONE" net) |
| 15 | PA5 | TX DTMF | DAC2 out: DTMF and tones into the transmit audio (R526, C560 to MIC) |
| 16 | PA6 | DIG-RXD | USART3, IOMUX 8: link to the HF6853 |
| 17 | PA7 | DIG-TXD | USART3, IOMUX 7: link to the HF6853 |
| 18 | PB0 | KEYOUT4 | Key matrix column 4 |
| 19 | PB1 | DIG POWER-EN | HF6853 power / enable (its CEN pin, pulled down by R717 10 kΩ) |
| 20 | PB2 | LOW BATTERY DET | Battery voltage divider (R525 10 kΩ into C509 / C550), ADC1 IN20 |
| 21 | PB10 | DIG/ANA SW | Analog or digital modulation path into the BK4819 (section 3) |
| 22 | PB11 | KEYIN4 | Key matrix row 4 |
| 23 | PF8 | DIG/ANA AF-SW | Analog or digital receive audio to the amplifier (section 3) |
| 25 | PB12 | W25Q-CS | Data flash chip select (GPIO) |
| 26 | PB13 | W25Q-SCK | SPI2 SCK, IOMUX 5 |
| 27 | PB14 | W25Q-DO | SPI2 MISO, IOMUX 5 |
| 28 | PB15 | W25Q-DI | SPI2 MOSI, IOMUX 5 |
| 29 | PA8 | KEYIN2 | Key matrix row 2; also side key on J500 |
| 30 | PA9 | KEYIN3 | Key matrix row 3; also side key on J500 |
| 31 | PA10 | UV APCOUT-SW | Selects the VHF or UHF power control output |
| 32 | PA11 | TXD | USART6 TX, IOMUX 8: PC link, through R504 1 kΩ to the speaker jack J502 |
| 33 | PA12 | RXD/PTT | USART6 RX, IOMUX 8: PC link on the microphone jack J501, **and** the PTT key (to ground through R520, R521) |
| 34 | PA13 | TX-LED/PDA | Red LED, and SWDIO (J504 pin 2) |
| 35 | PF6 | LCD-RST | Display reset |
| 37 | PA14 | RX-LED/PCK | Green LED, and SWCLK (J504 pin 1) |
| 38 | PA15 | LCD-CS | Display chip select |
| 39 | PB3 | LCD-SCK | Display clock, bit-banged |
| 40 | PB4 | LCD-CD | Display command / data |
| 41 | PB5 | LCD-SDA | Display data, bit-banged |
| 42 | PB6 | DCS-DET | Sub-audio detect from the RF sheet |
| 43 | PB7 | SDA4819 | BK4819 data, through R540 100 Ω; shared with the HF6853 |
| 44 | BOOT0 | | Selects Artery's ROM bootloader |
| 45 | PB8 | SCN4819 | BK4819 chip select, through R539 100 Ω; shared with the HF6853 |
| 46 | PB9 | SCK4819 | BK4819 clock, through R538 100 Ω; shared with the HF6853 |
| 1, 24, 36, 48 / 47, 8, 9 | | VDD / VSS, VSSA, VDDA | |

| Header | Pins | Notes |
|---|---|---|
| J504 (not fitted) | 1 RX-LED/PCK (SWCLK), 2 TX-LED/PDA (SWDIO), 3 3V3, 4 GND | SWD. The LEDs are on the same pins (schematic) |
| J700 "DIG-TEST" | 1 DIG-TXD, 2 DIG-RXD, 3 GND | The MCU-HF6853 UART, for a logic analyzer (schematic) |
| J500 "UV83PK" | 1 GND, 2 KEYIN3, 3 KEYIN2, 4 RXD/PTT, 5 GND | The side key flex: PTT and the two side keys pull their line to ground (schematic) |

## 3. RF and audio paths

| Item | Finding |
|---|---|
| Bands | TX and RX 136-174 and 400-470 MHz (FCC ID 2AZSA-RT4D lists 136-174 and 400-470). Receive-only ranges as sold: 18-108 MHz (FM), 108-136 MHz (AM), 174-350, 470-620, 840-999 MHz ([miklor](https://miklor.com/COM/Review_RT4D.php), unverified) |
| Power | Two levels. The reviewer measured 5.4 / 1.4 W on VHF and 5.1 / 2.1 W on UHF ([miklor](https://miklor.com/COM/Review_RT4D.php)) |
| BK4819 bus | 3-wire (SCN, SCK, SDA), bit-banged. **Two masters**: the MCU (PB7-PB9 through 100 Ω) and the HF6853 (its pins 12-14 through 100 Ω). The HF6853 drives the BK4819 itself in DMR mode, so the MCU must leave PB7-PB9 as inputs whenever the HF6853 owns the bus (schematic; the hand-over sequence is an open question, 7.3) |
| Modulation in | DIG-MOD (HF6853 AUDLP through R718, C727) and ANA-MOD (the MCU side) meet at the BK4819's modulation input through the DIG/ANA SW switch, PB10 (schematic) |
| Receive audio | Two BL1555 dual SPDT analog switches (U504, U505; 3V3). Each half connects COM to NC0 with its control low and to NO1 with it high (pin names). U504 half 2, control PB10: the BK4819's AF OUT4819 to 2NC0 (low), which goes through R523 4K7 to U505's 2NC0, or to DIG-AFIN, the HF6853's input (high). U505 half 2, control PF8: the AUDIO net (C564 to VR500 and the LN4898) from 2NC0 (low: the BK4819 path) or from DIG-AFOUT, the HF6853's decoded audio (high). FM receive audio therefore needs PB10 low and PF8 low (schematic). With PB10 low and PF8 high the speaker was silent (measured, 2026-11-11). DMR receive audio needs PF8 high: the stock application drives it high when a call is heard (5.4.2) |
| Modulation switches | U504 half 1, control PB10: DIG-MOD4819 (BK4819 MICP) to C534 to ground (low) or to DIG-MOD, the HF6853's audio out (high). U505 half 1, control PB10: the microphone (MIC) to ANA-MOD, R524 330 Ω to BK4819 MICN (low), or to DIG-MICINP, the HF6853's microphone input (high). FM transmit needs PB10 low (schematic) |
| Microphone | MK500 on the front, or the jack J501. MIC goes to the BK4819 for FM and to the HF6853 (DIG-MICINP, MICLP) for DMR (schematic) |
| DMR vocoder | Inside the HF6853: AMBE+2 class (unverified). The MCU never sees voice frames (section 5) |
| UV APCOUT-SW (PA10) | Selects which PA's power control the BK4819's APC output feeds: high for the VHF PA, low for the UHF PA (schematic; levels from stock 3.16, 3.2) |
| MCU audio taps | PA4 (pin 14) reaches the AUDIO net, the receive audio before the volume control, through R536 47 kΩ, the TONE node and C551 100 nF; C552 from TONE to ground is not fitted. The stock application uses it as a DAC output for tones into the speaker path. As an ADC input it would see the receive audio AC coupled, with no DC bias on the TONE node. PA5 (pin 15, TX DTMF, DAC2) goes to the transmit audio side (schematic). Whether receive audio sampled on PA4 and audio from PA5 into the transmitter are usable is unmeasured |
| Sub-audio | The BK4819 decodes CTCSS and DCS itself; DCS-DET (PB6) is an extra detector line (schematic) |

### 3.1 BK4819 control

The BK4819 register map and the driver are shared ([bk4819 driver](../../drivers/rf/bk4819/bk4819.h)). This table is
what the RT-4D board adds: how its GPIO outputs are wired and the order the stock application uses them.

| BK4819 GPIO | Net | Use (schematic; order from stock 3.16) |
|---|---|---|
| GPIO0 | DCS-DET | Not driven by the stock application |
| GPIO1 | U-TX | UHF transmit path on |
| GPIO2 | U-RX | UHF receive path on |
| GPIO3 | APC | PA power control on, during transmit |
| GPIO4 | V-TX | VHF transmit path on |
| GPIO5 | V-RX | VHF receive path on |

REG_33's output bit for GPIO *n* is `0x40 >> n`, as `bk4819_gpio()` writes it.

| Step | Stock sequence (stock 3.16) |
|---|---|
| Init | REG_00 = 0x8000 then 0x0000 (soft reset); REG_37 = 0x1D0F; REG_33 = 0x0100; REG_34 = REG_35 = 0; REG_1E = 0x4C58; REG_1F = 0xA656; REG_3E = 0xA037; REG_7D = 0xE950; REG_26 = 0x13A0. AGC: REG_13 = 0x03BE, REG_12 = 0x037B, REG_11 = 0x027B, REG_10 = 0x007A, REG_14 = 0x0019, REG_49 = 0x2A38, REG_7B = 0x8420 |
| Audio | REG_48 = 0xB32A (AF output gain), written during setup and again when the radio returns to analog. REG_47 = `0x6040 \| (n << 8)`: n = 0 mutes the audio output (squelch closed), n = 1 is FM audio (squelch open, and in transmit); the stock application also uses n = 2, 3, 5 and 7 for other modes (not decoded). A RadioFoundry image that never wrote REG_48 gave no speaker audio with the squelch open (measured, 2026-11-11) |
| Receive | REG_37 = 0x1F0F; REG_30 = 0x0200 then 0xBFF1. GPIO2 (UHF) or GPIO5 (VHF) on, the other off |
| Transmit | GPIO2 and GPIO5 off. Frequency (REG_38/39, with the calibration offset, 3.2). REG_37 = 0x1D0F; REG_52 = 0x028F; REG_30 = 0x0200, 0xC1FE, 0xC3FA. UHF: GPIO1 on, PA10 low. VHF: GPIO4 on, PA10 high. Then GPIO3 on and REG_36 for the power level |
| REG_36 | `(bias << 10) \| 0x80 \| 0x7F`: bias is the 6-bit calibration byte for the frequency and power level (3.2); bit 7 is set when bias is non-zero |
| Off | REG_30 = 0x0000; REG_37 = 0x1D00 |
| Measured | A RadioFoundry image ending transmit on REG_30 = 0xC3FA sent a carrier with no microphone audio; with 0xC3FE (bit 2 set) the voice is clean but quiet on another radio. FM receive and transmit voice both work with PB10 low and PF8 low (measured, 2026-11-11) |

UHF versus VHF is set by the calibration band (3.2): bands from 240 MHz up take the UHF path, those below take the VHF
path.

### 3.1.1 Frequency detect

The stock menu's "Freq Detect" finds the frequency of a nearby transmitter with the BK4819's frequency scan (stock
3.16):

| Step | Sequence |
|---|---|
| Range | The user picks one of the eleven ranges of 3.2 ("Detect Range", 18-64 ... 920-1000 MHz). Ranges 0-3 (below 240 MHz) switch on V-RX (GPIO5) and U-RX off; the others U-RX (GPIO2) and V-RX off |
| Start | REG_32 = 0x0B01 (frequency scan on) |
| Poll | REG_0D bit 15 set: no result yet. Otherwise the frequency in 10 Hz units is `((REG_0D & 0x07FF) << 16) + REG_0E + 0x8028 − ref`, where ref is the band record's reference offset (3.2, offset 0x00) |
| Give up | After 10 polls without a result, REG_32 = 0 (scan off) and the search starts again. A result outside the chosen range is dropped |
| Found | REG_32 = 0; the stock application shows the frequency rounded to the channel step and can tune to it. The CTC/DCS scan (a separate menu entry) then finds the tone |

### 3.2 Calibration

The calibration is in the SPI NOR data flash: one record of 0x70 bytes per band at offset `0x20 + band × 0x70`
(bands 0-10, so 0x020-0x4EF). The stock application reads the record for the band of the current frequency (stock
3.16).

| Band | Range (MHz) | Path | Index base (MHz) |
|---|---|---|---|
| 0 | 136-174 | VHF | 135 |
| 1 | 400-480 | UHF | 400 |
| 2 | 18-64 | VHF | 15 |
| 3 | 64-136 | VHF | 60 |
| 4 | 174-240 | VHF | 170 |
| 5 | 240-320 | UHF | 240 |
| 6 | 320-400 | UHF | 320 |
| 7 | 480-560 | UHF | 480 |
| 8 | 560-660 (the stock menu says 560-620) | UHF | 560 |
| 9 | 840-920 | UHF | 840 |
| 10 | 920-1000 | UHF | 920 |

Each band's 16 table entries are 5 MHz apart: the index is `(f − base) / 5 MHz`, capped at 15. Frequencies are in
10 Hz units in the stock application.

| Record offset | Size | Contents (stock 3.16) |
|---|---|---|
| 0x00 | u16 LE | Reference offset: REG_38/39 get `f + value − 0x8000` (10 Hz units) |
| 0x10 | 16 × u8 | REG_36 bias when the channel's power setting is 1. The stock menu lists "Low, High" in that order, so 1 is probably high power (inferred) |
| 0x20 | 16 × u8 | REG_36 bias for the other power setting (probably low) |
| 0x30, 0x40 | 16 × u8 each | Squelch thresholds for REG_4F and REG_78 when a channel byte is 0. The stock menu lists "Wide, Narrow" in that order, so this is probably wide (inferred) |
| 0x50, 0x60 | 16 × u8 each | The same when that byte is not 0 (probably narrow) |

The squelch registers are written as two bytes, `value + 2 + k` (high) and `value − 4 + k` (low), where *k* comes from
a table indexed by the squelch level (stock 3.16).

Each radio has its own calibration in its flash. A port reads it and leaves it unchanged.

### 3.3 Data flash addresses

Offsets the stock application reads, erases or writes with a constant address (stock 3.16). Only the calibration and
the font are identified.

| Offset | Contents |
|---|---|
| 0x000000 | Sector holding the calibration records (3.2) |
| 0x002400, 0x003000, 0x004000, 0x01C000, 0x05C000, 0x07C000, 0x082000, 0x088000, 0x094000, 0x0C5000, 0x0D6000 (5 bytes), 0x0D6005 (0x30 bytes), 0x0D7000 | Settings, codeplug and lists; not yet identified |
| 0x14C000 | Font |

## 4. Display, keys, LEDs, power

| Item | Finding |
|---|---|
| Panel | 1.44" 128x64 monochrome LCD, "GV12864GFFL021-16815A", serial mode (BM0, BM1 low; DB0-DB5 tied, DB6 = SCK, DB7 = SDA) with an internal charge pump (VB0±, VB1± caps) (schematic) |
| Controller | ST7565 / ST7567 command set. The stock initialization after the reset pulse on PF6: 0xE2 (reset), then 0x2C, 0x2E, 0x2F a few ms apart (booster, regulator, follower on), 0x25 (regulator ratio), 0x81 and a contrast byte (the stock application sends its contrast setting + 5: the setting runs 0-30, so the byte is 0x05-0x23, and before the settings are loaded it sends 0x12), 0xA2 (bias 1/9), 0xC8 (COM reverse), 0xA0 (SEG normal), 0x40 (start line 0), 0xB0 (page 0), 0xA6 (normal), 0xAF (on) (stock 3.16) |
| Orientation | With the stock 0xC8 (COM reverse) and 0xA0 (SEG normal), a frame sent page 0 first, column 0 first shows upside down; with 0xC0 (COM normal) it shows upright (measured, 2026-11-11). The stock application must flip its frame itself |
| Backlight | PA3 through Q501 to the LCD LED and five white keypad LEDs (schematic) |
| Key matrix | 4 columns (KEYOUT1-4: PA0, PA1, PF0, PB0) by 4 rows (KEYIN1-4: PF1, PA8, PA9, PB11), each row with 1 nF to ground (schematic) |
| Matrix layout | Column 1: F, 1, 4, 7. Column 2: Up, 2, 5, 8. Column 3: Down, 3, 6, 9. Column 4: EXT, \*, 0, # (rows 1-4 top to bottom) (schematic, switch labels) |
| Keys to ground | A/B (S517) on KEYIN1; the side keys on KEYIN2 and KEYIN3 (J500); PTT on RXD/PTT (PA12). A row reads low with every column high when one of these is pressed (schematic) |
| PTT and the PC link | PTT pulls the USART6 RX line low. Bytes from the PC do the same for a moment, so PTT needs the shared-line gate ([gotchas.md](gotchas.md) 16.2, `drivers/input/sharedptt`). The mic jack's PTT (J501) is on the same line |
| LEDs | Two-color D504: red on PA13 (TX), green on PA14 (RX) (schematic). Charge LED D900 driven by the charger, not the MCU |
| Battery | 3500 mAh Li-ion ([miklor](https://miklor.com/COM/Review_RT4D.php)). Measured on PB2 through R525 10 kΩ; the divider's lower leg isn't on the MCU sheet (schematic) |
| Battery scale | The stock application samples PB2 with ADC1 by DMA and computes `raw × 4 / 66`. Its thresholds on that value are 58, 60, 63 and 64, and at boot it waits until the value reaches 60. Read as 0.1 V units that is a two-cell pack (5.8-6.4 V warnings), about 0.0606 V per count, 24.8 V full scale with a 12-bit result (stock 3.16; the unit and cell count are inferred, unverified) |

## 5. The HF6853 DMR chip and its UART protocol

The HF6853 is a complete DMR terminal, not a baseband modem: it runs the air interface, the vocoder and call control
from its own firmware, controls the BK4819 in DMR mode, takes microphone audio and gives decoded audio. The MCU sets it
up and starts calls over a UART with a framed command protocol, the same one NiceRF documents for its DMR818 / DMR858
modules (module manual, stock 3.16). So the MCU sees calls, IDs and short data, never bursts or voice frames, and the
`hal_dmr_modem.h` contract (burst level) can't sit on it (section 7.2).

### 5.1 Link

| Item | Finding |
|---|---|
| UART | USART3 on PA6 / PA7, **115200** 8N1, receive interrupt (stock 3.16). The NiceRF modules default to 57600 (module manual); the RT-4D's FM100B firmware runs at 115200 |
| Power | DIG POWER-EN (PB1) to the HF6853's CEN (schematic) |
| Pass-through | The stock application can bridge USART3 to the PC link (USART6) both ways; Radtel's "HF685X Software upgrade tools" updates the HF6853 that way at 115200 (stock 3.16) |
| Firmware | Shipped separately: `FM100B_V1.2.0.13_20250430.bin` (1,527,808 B) in the V3.16 package. A security fix needs HF6853 firmware 01.02.00.13 or later ([radioreference](https://forums.radioreference.com/threads/chinese-backdoor.487138/page-5), unverified) |

### 5.2 Frame

Big-endian throughout (module manual, stock 3.16):

| Offset | Size | Field | Values |
|---|---|---|---|
| 0 | 1 | Head | 0x68 |
| 1 | 1 | CMD | Command code (5.4) |
| 2 | 1 | R/W | 0x00 read, 0x01 write, 0x02 sent by the chip on its own (a report). Replies to a request carry 0x01 or 0x02; the stock application treats 0x02 as a report |
| 3 | 1 | S/R | In a request 0x01. In a reply the status: 0x00 done, 0x01 busy or failed, 0x02 no channel, 0x07 module killed (stunned), 0x09 checksum error; some commands put a result code here (5.4) |
| 4 | 2 | Checksum | 16-bit one's complement sum of the frame as big-endian words (an odd last byte is the high byte of a word), carries folded in, then inverted. The module manual computes it with this field taken as zero; the stock application fills it with 0xFFFF first. A sender may put 0x0000 here and the chip skips the check (module manual); the stock application does that for command 0x25 |
| 6 | 2 | LEN | Bytes of DATA |
| 8 | LEN | DATA | |
| 8+LEN | 1 | Tail | 0x10 |

A frame is LEN + 9 bytes. The stock application accepts a received frame when byte 0 is 0x68, the length matches LEN
+ 9 and the checksum matches (stock 3.16).

### 5.3 Pacing

The stock application sends one request at a time: it waits at least 100 ms after the previous request, then polls
for the reply's status for up to 100 ms (most commands), 200 ms (0x06), 2 s (0x0A, 0x48) or 4 s (0x07), and gives up
after 6 tries (stock 3.16).

### 5.4 Commands

Base set, as the module manual names them; "RT-4D" says whether the stock application sends it (S), handles it from
the chip (R) or both:

| CMD | Function (module manual) | RT-4D |
|---|---|---|
| 0x01 | Channel change | |
| 0x02 | Receive volume | S, R |
| 0x03 | Scan on / off | R |
| 0x04 | Transceiver status (receiving, transmitting, standby) | R |
| 0x05 | RSSI (5.4.3) | S (1 byte, 0x02), R (S/R 0x00, raw level in DATA byte 0) |
| 0x06 | Call start / stop: call type (private, group, all), number | S (5 bytes: type, 4-byte ID), R |
| 0x07 | SMS send (IP confirmed / unconfirmed, group) | S, R |
| 0x09 | Emergency alarm | S, R |
| 0x0A | "Enhancements": radio check, remote monitor, stun, revive. Results 0xA1-0xA5, 0xAF | S (5 bytes: function, 4-byte ID), R |
| 0x0B | Mic gain 0x00-0x0F | S, R |
| 0x0C | Power-saving mode | S, R |
| 0x0D | TX / RX frequency | |
| 0x0E | Repeater or direct | |
| 0x10 | Caller: call type and number | |
| 0x11 | Read received SMS | |
| 0x12 | Squelch | |
| 0x13 / 0x14 | CTCSS / DCS mode and code | |
| 0x15 | Monitor | |
| 0x16 | Bit error rate | R |
| 0x17 | High / low power | S, R |
| 0x18 | Contact | |
| 0x19 | Encryption on / off | R |
| 0x1A | Initialization complete (report) | R |
| 0x1B | Own number | |
| 0x22, 0x24, 0x26, 0x27, 0x28 | Read contact, own ID, contact list, scan state, encryption state | |
| 0x25 | Firmware version | S, R |
| 0x29, 0x30 | Add / delete a receive group | |
| 0x31, 0x32, 0x33 | Own color code, analog bandwidth, time slot | |
| 0xF0, 0xF2 | Factory defaults, software reset | |

What the stock application sends with three of them (stock 3.16): 0x81 an encryption key with its type (5.4.1), 0x82 20 bytes of channel settings (5.4.1), 0x84 a list read from the data flash (contacts or a receive group, 0x30 and 0x20 bytes); 0x83 from the chip carries one status byte. The HF6853 firmware file (FM100B V1.2.0.13) names its handlers: besides calls, SMS, scan and settings it has record and playback of received voice (`ATC_RecordEnReq`, `ATC_LocalRecordPlayReq`, `ATC_RecordDataPlayReq`, `ATCRecvAmbedataCnf`), in-band data and a `mac_phy_bypass` debug string. None of the names is a repeater mode or raw burst access, so the chip offers no path to the burst-level SFR of [sfr.md](sfr.md) (stock 3.16, the HF6853 firmware's strings).

Commands the stock application uses that the module manual doesn't list: sends 0x2A (4 bytes), 0x3E (15 bytes), 0x42, 0x45, 0x48, 0x4C, 0x4D,
0x55 (1 byte each), 0x57, 0x62, 0x84 (2 bytes), 0x81 (variable), 0x82 (20 bytes); handles 0x3D, 0x3E, 0x42, 0x4C,
0x59, 0x5D, 0x60, 0x83 from the chip as well (stock 3.16). 0x62 is promiscuous mode (5.4.1) and 0x59 the
carrier report (5.4.3); the meaning of the others is open (7.3).

#### 5.4.1 Channel settings and encryption key (stock 3.16)

Multi-byte values in these DATA fields are big-endian, like LEN. A DMR channel change sends, in this order:
0x2A (our own radio ID, below), 0x82, the receive group (0x17 with one byte when the channel has none, else 0x84
with the list), 0x81, 0x62 (promiscuous, below), 0x0C (1 byte, 0).

Command 0x2A, 4 DATA bytes: our own radio ID, as 8 packed BCD digits in little-endian byte order (the one
exception to big-endian here: ID 1234567 is 0x67 0x45 0x23 0x01). The stock application sends the channel's own ID
when the channel record's byte 0 is 1, else the radio-wide ID from its settings; the menu converts the entered
decimal number to BCD before storing it. It is not the TX contact: the call target goes in 0x06 (5.4.2).

Command 0x82, 20 DATA bytes:

| Byte | Contents |
|---|---|
| 0 | Time slot: 1 or 2 |
| 1 | Color code 0-15 |
| 2 | Dual capacity direct mode (the menu's "Dual Slot"): 0 off, 1 on |
| 3-6 | RX frequency in Hz |
| 7-10 | TX frequency in Hz |
| 11 | 4 (constant) |
| 12-15 | 0xAA 0xAA 0xAA 0xAA (constant) |
| 16 | 0 (constant) |
| 17, 18 | Two channel bytes, not identified yet (7.3); the TX admit menu (Impolite, Carrier Match, CC Match) is a candidate |
| 19 | The channel's key number (below), 0 none |

Command 0x81 loads the key for the channel, or turns encryption off:

| Byte | Contents |
|---|---|
| 0 | Key number 1-n; 0 turns encryption off |
| 1 | Type: 0 off, 1 ARC (5-byte key), 4 AES-128 (16 bytes), 5 AES-256 (32 bytes) |
| 2.. | The key bytes, as many as the type has; none when off |

LEN is 2 + the key length (2, 7, 18 or 34). The stock application keeps the keys in the data flash at 0x82000, one
48-byte record per key number (record n at `0x82000 + (n − 1) × 48`): byte 1 the type (0 ARC, 1 AES-128, 2 AES-256),
bytes 16 onward the key. The key bytes go to the chip unchanged.

Command 0x62, 2 DATA bytes: 1, then promiscuous mode, 1 on or 0 off (bit 0 only). With it on, the chip plays every
call on the channel's slot and color code, not only the receive group's. The stock application keeps it per channel
(a menu item "Promiscuos", On or Off) and sends 0x62 on each channel change, when the item is changed, and again
after each call heard ends (0x6F, 5.4.2). Its 0x62 frame carries checksum 0xFFFF.

#### 5.4.2 Calls (stock 3.16)

Command 0x06 starts and ends a call. S/R is 0x01 for PTT down and 0xFF for PTT up; LEN 5; the reply time-out is
200 ms (5.3):

| Byte | Contents |
|---|---|
| 0 | Call type: 1 private, 2 group, 4 all call |
| 1-4 | The target ID as 8 packed BCD digits, most significant first (ID 1234567 is 0x01 0x23 0x45 0x67). All call sends 0xAA 0xAA 0xAA 0xAA |

The stock application sends the same frame for PTT up as for PTT down, with only S/R changed. Its contact records
(data flash 0x5C000, 32 bytes each) hold the type in byte 1 (0 private, 1 group, 2 all call, sent as 1, 2, 4) and
the ID in bytes 2-5 already in BCD; a manually dialed number is converted from decimal.

From the chip, a 0x06 frame carries an event code in S/R. Code 0x60 is a call heard; its DATA:

| Byte | Contents |
|---|---|
| 0 | Call type, as above (the stock receive screen shows SID, GID or AID for 1, 2, 4) |
| 1-4 | The called ID (target) |
| 5-8 | The caller's ID (source) |

The stock application takes both IDs as 4 big-endian bytes without conversion; that they are BCD like the TX side
is likely, not checked.

The reply to a 0x06 request is not checked for success: the stock application marks its wait (an internal byte set
to 0xFF), sends the request and takes any 0x06 frame from the chip as the end of the wait, whatever its S/R (up to
200 ms, 6 tries). What it does next follows the chip's 0x06 event codes, handled as a table over 0x60-0x75:

| S/R | Stock handling |
|---|---|
| 0x60 | A call heard: the call data above, receive audio opened (below) |
| 0x6F | The same checks as 0x60, then receive audio closed (below): the end of a heard call |
| 0x61 | Shows "Being Monitored" (a remote monitor of this radio) |
| 0x62 | Updates the call screen when a call state flag is set |
| 0x6C | Clears the call state, shows "Connect to" / "repeater failed" |
| 0x6D | Clears the call state |
| 0x6E | Sets a call state flag and updates the call screen |
| 0x75 | Sets the call state to 4 |
| 0x63-0x6B, 0x70-0x74 | Nothing |

Receive audio for a call heard (stock 3.16). The amplifier follows a set of requests (receive audio, beeps, tones);
a timer tick drives PA2 high while any request is set, and PA2 goes low when the last one is cleared.

| Event | Stock sequence |
|---|---|
| 0x60, open | PA13 low, PA14 high (red LED off, green on). Sets the receive-audio request (the amplifier on, PA2 high). PF8 high: the AUDIO net from DIG-AFOUT, the HF6853's decoded audio (3, Receive audio). Then the call screen |
| 0x6F, close | PA14 low (green LED off). 0x62 with the channel's promiscuous setting (5.4.1). The call screen cleared. Clears the receive-audio request (PA2 low unless another request is set). PF8 is left high; the next 0x82 or bus hand-over drives it low (5.5) |

The open runs again on each 0x60 of a call. No other place that closes the DMR receive audio was found: a call heard
ends at 0x6F, not on a time-out.

So a reply to PTT down whose S/R is not 0x00 is not a refusal on stock; which code the chip sends for a PTT that went
on the air, and for one it refused, is open (7.3).

#### 5.4.3 Signal level in DMR (stock 3.16)

The MCU can't read the BK4819's RSSI in DMR: the HF6853 owns its bus (5.5). The stock application asks the HF6853
instead:

| Step | Stock behavior |
|---|---|
| Carrier | The chip sends command 0x59 with DATA byte 0 = 1 when a signal is present and 0 when it is gone. On 1 the stock application sets a carrier flag (and the green LED, if a setting allows it); on 0 it clears the flag |
| Request | While the carrier flag is set, every 500 ms: command 0x05, R/W 0x01, S/R 0x01, LEN 1, DATA 0x02, checksum 0xFFFF. It waits up to 100 ms for the reply, 6 tries. Without a carrier it sends nothing and clears the level |
| Reply | Command 0x05 with S/R 0x00: DATA byte 0 is the raw level, an unsigned byte. Another S/R: no new level |
| Display | Bar steps = (raw - 21) / 2, at most 18; raw 21 or less is 0. No dBm value is shown |

So raw 21 is the stock application's floor, and raw 57 or more is the full bar. The scale of raw against dBm is
open (7.3).

The HF6853 firmware's later release (FM100B V1.2.0.32, in the vendor's 2026 update package) has the same handler
names as V1.2.0.13 as far as its strings show: still no repeater mode or raw burst access.

### 5.5 Power-up and bus hand-over

GPIO registers here are named as in the reference manual (RM V2.04): SCR sets bits, CLR clears them. The stock
application does the following (stock 3.16; delays as the stock application waits, not minimums).

| Step | Sequence |
|---|---|
| Bus to the HF6853 (DMR) | PF8 low (no receive audio until a call heard drives it high, 5.4.2). Wait 10 ms. PB7-PB9 to inputs, no pull. PB10 high (modulation from the HF6853) |
| Bus to the MCU (FM) | PB7-PB9 to push-pull outputs. Wait 50 ms. BK4819 init (3.1). PB10 low (modulation from the MCU side). PF8 high (receive audio closed). Opening FM receive audio writes REG_48 = 0xB32A and drives PF8 low; closing it drives PF8 high (stock 3.16; FM receive with PF8 low measured) |
| HF6853 on | Bus to the HF6853. PB1 high. Wait for the chip's 0x1A report (initialization complete) with status byte 0, up to 3 s. On a timeout: PB1 low, wait 3 s, try again |
| After the 0x1A report | Wait 500 ms; send 0x42 (1 data byte, 0xFF); wait 500 ms; send 0x3E (15 data bytes, each 0x0F); wait 100 ms; send 0x57, 0x0C (power saving, 0 = off), 0x45 (2), 0x48 (0x0F), then 0x4C (1 data byte, 0x01). These frames carry checksum 0x0000 (no check, 5.2) |
| HF6853 off (to FM) | Red and green LEDs off. Wait for the chip to finish what it is doing (a flag the receive handler sets; not identified). PB1 low. Bus to the MCU. Then the BK4819 receive path as in 3.1 |

The stock application powers the HF6853 at boot and hands the bus to the MCU when the channel is analog. The
meaning of 0x3E, 0x42, 0x45, 0x48, 0x4C and 0x57 is still open (7.3). An FM-only port keeps PB1 low and the bus with
the MCU, and never needs the HF6853.

## 6. PC link, update and recovery

| Item | Finding |
|---|---|
| Cable | K1 (Kenwood two-pin) programming cable with its own USB-UART; RX on the mic jack, TX on the speaker jack (schematic, [miklor](https://miklor.com/COM/Review_RT4D.php)) |
| Bootloader entry | Hold PTT while switching the radio on (stock 3.16, the updater's instructions) |
| Line | 115200 8N1 (stock 3.16) |

### 6.1 Vendor bootloader protocol

As the V3.16 updater drives it (stock 3.16). The radio answers every frame with one byte: 0x06 accept, 0xFF error
(the updater stops), 0x00 or 0x32 ignored by the updater.

| Step | PC sends | Radio |
|---|---|---|
| Connect | `39 33 05 10 C9`, again every 200 ms up to 4 times until a byte comes back | 0x06 |
| | The same connect frame twice more, each after the previous 0x06 | 0x06 each time |
| Start | `39 33 05 55 0E` | 0x06 |
| Image check | The bootloader starts the application only if its first word (the initial stack pointer) is below the end of RAM: 0x2000C000 is refused (the radio stays dark), 0x2000B000 starts. The stock image has 0x2000B510 (measured, 2026-11-11) |
| Data | 246 frames of 1028 bytes, each after the previous 0x06 (none awaited after the last): 0x57, the image offset's bits 15-8, its bits 7-0 (always 0, since frames are 1024 bytes apart; the 16-bit field wraps every 64 KB), 1024 image bytes, check byte | 0x06 |
| | `39 33 05 EE A7` exists in the updater but is never sent | |

The last byte of every frame is the 8-bit sum of the bytes before it plus 0x48. Image offset 0 is 0x08002800; the
246 frames cover the whole application region, so a RadioFoundry image is padded to 251,904 bytes. The updater waits
50 ms after each frame it sends.

### 6.2 Other paths

| Path | Notes |
|---|---|
| Artery ROM bootloader | BOOT0 high at reset, then USART1 (PA9 / PA10), USART2 (PA2 / PA3), USART3 (PB10 / PB11) or USB (PA11 / PA12) (datasheet). On this board those pins are keys, the amplifier enable, the backlight and the PC link without its USB data lines, so the ROM bootloader is reachable only with the radio open (unverified) |
| SWD | J504, with the radio open. The stock application may enable access protection (unverified) |

### 6.3 Debug link keys

The `board` view section ([debuglink.md](debuglink.md) 3.7) carries these keys, each as `board.<key>`. The audio
path's state, read live from the chip and the pins:

| Key | Value |
|---|---|
| `bk4819.reg47`, `bk4819.reg48` | REG_47 (AF output select) and REG_48 (AF gain) as the chip reads them (3.1, Audio); `none` when the chip didn't answer at init |
| `bk4819.reg30`, `bk4819.reg37` | REG_30 (enables) and REG_37 (power) |
| `amp_en` | PA2, the LN4898 enable: 1 = on (FM: the squelch open; DMR: a call heard) |
| `af_sw` | PF8: 0 = the BK4819's audio (FM receive open; in DMR, between calls and in transmit), 1 = the HF6853's audio (a DMR call heard; in FM, receive muted) |
| `audio_open` | the core opened the receive audio (squelch open) |
| `mod_sw` | PB10: 0 = microphone and receive audio on the BK4819 path (FM), 1 = the HF6853's |

The HF6853 driver's keys follow, each as `board.dmrc.<key>`, in this order; the `dmr_stats` section has the first 16
of them as `dmrc.<key>` (`hal_dmr_call_t.view_line`). While the HF6853 holds the bus the BK4819 keys read `dmr`:

| Key | Value |
|---|---|
| `state` | the engine: `off`, `wait` (for the 0x1A report), `cool` (powered off before another try), `startup`, `ready` |
| `last_reply` | the last reply to one of our requests, `cmd/S-R` in hex: after a PTT, the S/R the chip answered 0x06 with |
| `sr06` | the last 4 distinct S/R values on 0x06 frames from the chip, newest first (replies and events alike, 5.4.2) |
| `power_ups frames_tx frames_rx timeouts give_ups nacks` | counters: power-ups, frames sent and received, replies that didn't come, requests given up after 6 tries, replies with a status other than done |
| `rx_bad rx_long rx_unchecked` | received frames with a wrong tail or checksum, with DATA over 48 bytes, taken with checksum 0x0000 |
| `reports last_report` | frames from the chip the driver doesn't read, and the last of them as `cmd/R-W/S-R/LEN:` and up to 8 DATA bytes in hex (none for 0x81) |
| `seen` | the commands the chip sent, in the order first seen |
| `groups` | the receive group list's length (not sent: 5.4.1) |
| `on_air` | 1 from the first PTT down sent until a PTT up is answered (5.4.2) |
| `heard heard_events heard_ends heard_quiet heard_gap_max_ms` | a call heard now (0x60, 5.4.2); the 0x60 events received; calls heard ended by 0x6F, and by the quiet time-out instead; the longest gap between two 0x60 for the same call |
| `carrier` | the chip's last 0x59 report: 1 a signal present (5.4.3) |
| `rssi_raw` | the last level 0x05 answered with, raw (5.4.3); the dBm from it is `rssi_dbm` and `call.rssi_dbm` in the core's view |
| `hear_all` | what the last 0x62 sent said: 1 promiscuous (the digital monitor), 0 off, `none` before one was taken (5.4.1) |
| `heard_raw` | the last 0x60 event's DATA as received, `type:called:caller` in hex, to check the BCD reading of the IDs |
| `heard_not_bcd heard_ignored` | IDs that weren't BCD (read as plain big-endian numbers); 0x06 events in 0x60-0x75 other than 0x60 and 0x6F, not read yet (their codes are in `sr06`) |

## 7. What a port needs

### 7.1 Reuse and new work

| Part | In RadioFoundry | New |
|---|---|---|
| MCU | `platform/stm32f4` is a different family | `platform/at32f423`: clocks (CRM, internal oscillator), GPIO and IOMUX (laid out like the STM32F4's), USART, SPI, ADC, DAC, timers, flash. Artery's own library is not under a license this tree accepts; registers come from the reference manual (V2.04) and the SVD |
| RF | `drivers/rf/bk4819`, `drivers/rf/subaudio` | The board's band and PA switching; bus hand-over to the HF6853 |
| Display | `drivers/display/st7567`, the 128x64 UI class | Bit-banged bus glue |
| Keys | `drivers/input/keymatrix`, `drivers/input/sharedptt` | Direct-to-ground keys in the scan |
| Data flash | `drivers/flash/spi_nor` | |
| Broadcast FM | `drivers/bcast/rda5807m` is a different chip | A BK1080 driver, with `CONFIG_BCAST_FM` |
| DMR | None: the HF6853 isn't a burst-level modem | A driver for the HF6853 protocol, and a call-level DMR interface in the core (7.2) |
| Update | | The vendor bootloader protocol (6.1) in the web flasher and `rfoundry` |

### 7.2 Budget

| Item | RT-4D | RadioFoundry today |
|---|---|---|
| Flash | 246 KB for the application | An FM-only MD-9600 build (`CONFIG_DMR=n CONFIG_USB=n CONFIG_VFO=n CONFIG_DIAG=n CONFIG_SUBTONE=n CONFIG_FONT_XL=n`) is 191 KB of code; the default MD-9600 build is 478 KB |
| RAM | 48 KB, no CCM | The same FM-only build uses about 77 KB: a 28 KB FreeRTOS heap, about 34 KB of static data and buffers, plus stacks |

FM fits the flash. RAM has to come down by about 30 KB before anything runs. Without the burst-level DMR stack (the
HF6853 replaces it), DMR on this radio is a small driver plus UI, not the core's DMR layers.

The core's DMR features (`CONFIG_DMR` and everything under it: hotspot, SFR, privacy, data, the IP gateway) assume a
burst-level modem. On the HF6853 they are `BOARD_UNSUPPORTED` except what the chip offers itself (calls, SMS, radio
check, emergency, its own encryption). Giving DMR calls to this radio needs a call-level interface in `hal/` that the
UI's call screens can use with either stack: the call-level interface of
[dmr-call-level.md](dmr-call-level.md), which serves any chip that runs DMR itself.

### 7.2.1 Call-level DMR

Built (`CONFIG_DMR_CALL`, on by default), not yet run on the radio. The generic design is
[dmr-call-level.md](dmr-call-level.md); this is what it is here.

| Layer | File | What it does |
|---|---|---|
| Frame | `drivers/dmr/hf6853/hf6853_frame.c` | 5.2: head 0x68, CMD, R/W, S/R 0x01 in a request, the one's complement checksum (0x0000 for the start-up frames and 0x25, as stock sends them), LEN, tail 0x10. Received frames: the checksum checked, 0x0000 taken and counted, DATA over 48 bytes counted and skipped |
| Commands | `drivers/dmr/hf6853/hf6853_cmd.c` | 0x82 (5.4.1, bytes 17 and 18 sent as 0), 0x81 (key number, type 0 / 1 / 4 / 5, the key), 0x2A (our own ID: the channel's where it has one, else the radio's, little-endian BCD), 0x0C (0), 0x62 (1, then hear all 1 or 0), 0x06 (5.4.2: type, the ID in BCD, all call 0xAA), 0x05 (0x02) and the raw level to dBm (5.4.3); the 0x60 event read (IDs as BCD, a field that isn't BCD as a plain number and counted); the start-up of 5.5 without 0x57; the reply time-outs of 5.3 |
| Engine | `drivers/dmr/hf6853/hf6853.c` | 5.5: the 0x1A wait (3 s, then PB1 low for 3 s and again), the start-up, then 0x25 and the ready event. 5.3: one request in flight, 100 ms after the last, 6 tries (a time-out, or S/R 0x01 or 0x09). Three requests given up in a row: the link counts as lost, power off and up again. A 0x1A report later: the start-up runs again. Channel change in the stock order (5.4.1) without the receive group, whose bytes are open: 0x2A, 0x82, 0x81, 0x62, 0x0C. 0x62 carries the core's digital monitor (`dmrc_channel_t.hear_all`). The core sends the channel on every change of its configuration; a channel the chip already took isn't sent again, and a change of the monitor alone sends 0x62 alone. Calls (5.4.2): PTT up goes ahead of anything queued, PTT down after the channel's set-up. Any 0x06 frame from the chip answers our 0x06, as on stock: S/R 0x01 and 0x09 are tried again, 0x07 (killed) and 0x02 (no channel) refuse a PTT down, anything else takes it. From the first PTT down sent, a PTT up always follows, and a PTT up unanswered after 6 tries powers the chip off (fault 4). A call heard: the 0x60 event starts it, the same IDs again keep it, other IDs end it and start the next, 0x6F ends it, as on stock. A long quiet time-out (no 0x60 or 0x6F for longer than a call lasts) is only a fallback, should the 0x6F be lost on the link (`HF6853_HEARD_QUIET_MS`, 30 s). After each 0x6F, and after the fallback, 0x62 goes again (5.4.1). The other events of 0x60-0x75 are counted and their codes kept. The receive audio hook (`hf6853_io_t.rx_audio`) is on while a call is heard and no PTT down went out, off otherwise. Signal level (5.4.3): while the chip reports a signal (0x59 DATA 1) or a call is heard, 0x05 DATA 0x02 every 500 ms (`HF6853_RSSI_POLL_MS`), the first at once; last in the queue, never while a PTT waits or is on the air, tried once (a missed reply waits for the next turn and doesn't count against the link). A reply with S/R 0x00 gives the raw level, as `DMRC_EV_RSSI` in dBm when it changes. The raw-to-dBm line is a placeholder, uncalibrated (7.3): raw 21 (the stock floor) -120 dBm, raw 57 (its full bar) -60 dBm, straight between (`HF6853_RSSI_*` in `hf6853.h`) |
| Board | `boards/rt4d/dmr_rt4d.c`, `radio_rt4d.c` | USART3 at 115200 (PA6 IOMUX 8, PA7 IOMUX 7) with a 256-byte receive ring and interrupt transmit (`at32_aux_uart_*`, `platform/at32f423/uart.c`); PB1 the chip's CEN; `set_mode(DMR)` hands the bus and the audio over as 5.5 has it, `set_mode(FM)` takes them back and sets the BK4819 up again; the FM hooks leave the BK4819 alone meanwhile. In DMR the driver's receive audio hook (`rt4d_dmr_audio`) drives PF8 and the amplifier: on, PF8 high then PA2 on; off, PA2 off then PF8 low. `status()` gives the driver's level in DMR while it has one, else -160 dBm (nothing heard). The key store reads the stock records of 5.4.1 (16 of them; read only, as they sit below the flash glue's write floor) |

PF8 and the amplifier in DMR (5.4.2): low and off from the bus hand-over; on 0x60 PF8 high and the amplifier on; on
0x6F, or when a call heard ends any other way, the amplifier off and PF8 low (stock leaves PF8 high until the next
0x82; low is safe, as the speaker is off then and the BK4819's path carries nothing in DMR, PB10 high sending its
audio to the HF6853). Between calls and while we transmit both stay off, so the HF6853's output (the 4FSK buzz on
the air) never reaches the speaker. The radio has no beeps or tones in DMR (`CONFIG_BEEP` is not written), so the
amplifier serves the call heard alone. The green LED is the core's: it lights while the call heard plays
(`RT4D_KEEP_SWD` leaves PA13 / PA14 to the debugger as before). Promiscuous mode is 0x62 (5.4.1): the core's digital
monitor maps onto it, sent on each channel change, when the monitor is turned on or off, and after each 0x6F.

What it does on the radio today: the chip receives and plays calls itself on the channel's frequency, color code and
slot, with the channel's key. Each 0x60 event goes to the call-level core as a call heard, so the receive screen,
Recent and the reply window show it; 0x6F ends it. The speaker plays the call heard only. The digital monitor
(**Monitor** on a DMR channel) turns promiscuous mode on. The signal level shows on the receive screen and the status
bar in dBm, from 0x05 on the placeholder scale. PTT on a DMR channel sends 0x06 to the core's talk target (the
channel's TX contact, or the reply window's); the chip's reply puts the call on the air unless it says killed or no
channel (`tx_error=busy`). Which S/R means on the air is open: `last_reply` and `sr06` show what the chip sent. Not read yet: BER,
the receive group list, SMS and the other reports.

Encryption: `key()` sends 0x81 after 0x82 on each channel change: ARC, AES-128 or AES-256, or type 0 to turn
encryption off. The bytes in the stock record go to the chip unchanged. AES with a third-party radio works on stock
(owner report); test again with this driver.

| Budget | FM only (`CONFIG_DMR_CALL=n`) | With call-level DMR (the default) |
|---|---|---|
| Flash | 199,696 of 251,904 B | 232,000 B: 32,304 B for DMR (the DMR screens, contacts, Recent, the core's call-level part, the driver, the board glue); 19,904 B free. Hear all, the signal level and the receive audio hook are about 0.9 KB of it. Calls (5.4.2) are about 1.7 KB of it, `CONFIG_DMR_AES` 728 B. Both columns have the FM TX tones and DTMF (2,168 B) |
| RAM | 45,096 of 49,152 B | 49,064 B: 88 B free. The 14,336 B heap and the 1,536 B main stack are in it. The driver is about 340 B of state (one frame each way, the key until sent, the call and the call heard, the signal level) and the 256-byte ring |

How the RAM was found, each step measured: a stub driver first (flash 222,700 B; RAM 52,672 B with the 16 KB heap,
3,520 B over). Then, static: `radio_view_t`'s empty copy const (456 B), 9 cached talk group names (576 B), the debug
log ring at 512 B (512 B), the CPS request buffer at 1.5 KB (512 B), the view's talker alias at one byte without
`CONFIG_DMR_TALKER_ALIAS` (192 B), the UI's privacy toggle without a settings copy (396 B). On the heap: the radio
task's stack at 512 words without the burst-level stack (1 KB), the host link's at 496 words, the start-up task's at
512, the timer task's at 160 (`make stack-check`: the least free is the radio task's, 44 bytes over its margin), and
the heap from 16,384 to 14,336 B: the stacks (10,272 B), the kernel's task and queue blocks (about 2 KB) and some 2 KB
to spare. `tasks.heap_min_free` on the radio says whether that holds. `CPS_FAST` and `DUAL_WATCH` stay on.

### 7.3 Open questions

| Question | How to answer it |
|---|---|
| Confirm which REG_36 table is high power and which squelch table is wide (3.2) | The SDR bench: power into the dummy load at each setting |
| Data flash map beyond the calibration and font (3.3) | Stock application; reads of the flash on the radio |
| The extended HF6853 commands (5.4) | Stock application's callers; UART capture on J700 |
| Command 0x82's bytes 17 and 18 (5.4.1); 0x06 event codes besides 0x60, and whether received IDs are BCD (5.4.2); the largest SMS (0x07, 0x11) (7.2.1) | Stock application's callers; UART capture on J700 while the stock menu changes the channel's TX admit. On the radio: `dmrc.heard_raw` and `dmrc.heard_not_bcd` during a call from a radio whose ID is known |
| How often the chip repeats 0x60 during a call (stock ends a call at 0x6F only, 5.4.2) | `dmrc.heard_ends` against `dmrc.heard_quiet`, `dmrc.heard_gap_max_ms` |
| R/W in a 0x06 request (the driver sends 0x01, write, like the other requests); the S/R the chip answers a PTT with, on the air and refused (5.4.2: the driver refuses only on 0x02 and 0x07) | `dmrc.last_reply` and `dmrc.sr06` after a PTT on the radio; UART capture during a call from the RT-4D |
| The raw level of 0x05 (5.4.3) against dBm | A signal generator into the antenna port, or a second radio through a known attenuator: `dmrc.rssi_raw` at two or more levels |
| The reports 0x04 (state), 0x10 (caller), 0x16 (BER), 0x19, 0x83: their DATA layouts | UART capture during a call from a second radio; the driver keeps the last report and the commands seen (`dmrc.last_report`, `dmrc.seen`) |
| The receive group: 0x17's byte when the channel has none, 0x84's list format; 0x57's two bytes | Stock application's callers; UART capture on a channel change |
| 0x25's reply format (the driver shows its first 4 bytes raw as `dmrc.version`); which field of the 0x1A report holds its status (the driver takes S/R or DATA byte 0) | UART capture at power-up |
| Whether the HF6853 sets the PA from its own calibration in DMR, and how a power level reaches it | Stock application's callers; the SDR bench |
| Whether the HF6853 needs 0x62 sent again after 0x6F, as stock does (5.4.1) | A second call heard with promiscuous on |
| The number of stock key records at 0x82000 (the port reads 16) | Stock application; reads of the flash |
| The largest SMS (0x07, 0x11): the frame buffers are sized for the AES-256 key frame, 43 B, until it is known | Stock application's callers |
| Battery scale units and cell count (4) | A voltmeter on the radio against the debug link's raw ADC value |
| What else the vendor bootloader checks besides the initial stack pointer (6.1) | Images that differ in one word, on the radio |
