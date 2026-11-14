<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# Call-level DMR

**Status: built (`CONFIG_DMR_CALL`), not yet run on a radio.** How a radio whose DMR chip runs calls itself gets
DMR: the chip does the air interface, the vocoder and call control, and the MCU sees calls, IDs and short messages,
never bursts or voice frames. Section 10 says what is built and what waits for facts about the chip. Which radios have such a chip is in [feature-matrix.md](../feature-matrix.md)
and [radios.md](radios.md); each chip's protocol is on the radio's hardware page.

The burst-level stack ([dmr-voice.md](dmr-voice.md), [dmr-modem.md](dmr-modem.md), `hal/hal_dmr_modem.h`) stays as it
is. The call-level backend sits beside it, under the same UI and the same codeplug. A radio builds one or the other.

## 1. Goals and limits

| Item | Decision |
|---|---|
| What the UI sees | The same DMR screens, Quick tiles and view (`radio_dmr_view_t`) on both backends. A screen whose feature the backend lacks is not shown |
| What the codeplug holds | The same channels, contacts, RX group lists and zones. A channel's DMR fields mean the same on both |
| What the core decides | The talk target, the reply window, focus, names (codeplug contacts, the ID database), the call log, the inbox. The chip gets IDs and settings, never names |
| What the chip decides | Channel access, the air protocol, the vocoder, its own encryption, its own call filtering by RX group |
| Out of scope | Hotspot, SFR, IP over DMR, roaming by burst timing, our own privacy keystream: they need bursts (section 4) |

## 2. The hal interface: `hal/hal_dmr_call.h`

`board_t` gets `const hal_dmr_call_t *dmr_call;` (NULL on every radio without such a chip). A board has `dmr` (burst
level) or `dmr_call`, never both. Every operation queues a request and returns at once: true when it was taken, false
when the queue is full or the chip is off. The result comes back as an event (2.2). Nothing in the interface blocks.

### 2.1 Operations

| Operation | Arguments | Notes |
|---|---|---|
| `init()` | | Driver state, the UART. The chip stays off |
| `power(on)` | | On: power the chip, hand it the RF chip, wait for its ready report (`DMRC_EV_READY`). Off: the reverse. The board does the RF bus hand-over (2.4) |
| `poll(now_ms)` | | Runs the driver: frames in and out, pacing, retries, timeouts. Called from the radio task, never blocks (section 5) |
| `channel(const dmrc_channel_t *)` | RX and TX Hz, color code, timeslot, repeater or direct, power level, own ID, bandwidth for the chip's analog side if it has one | One call per channel change; the driver splits it into the chip's commands |
| `groups(const uint32_t *ids, n)` | The channel's RX group list | The chip filters group calls by it. `n = 0`: the talk group only |
| `call(const dmrc_target_t *, on)` | Call type (group, private, all call) and ID; on = PTT down, off = PTT up | The chip keys and unkeys. Talk permit and channel busy come back as events |
| `sms(dst, group, text, len)` | | Confirmed or not as the chip offers; the result is `DMRC_EV_SMS_DONE` |
| `function(fn, id)` | Radio check, remote monitor, stun, revive, call alert where the chip has them | Results as `DMRC_EV_FUNC_RESULT` |
| `emergency(on)` | | The chip's emergency alarm |
| `key(const dmrc_key_t *)` | The key to use, NULL for encryption off (section 6) | Called on every channel change and after power-up |
| `volume(level)`, `mic_gain(level)` | The chip's own receive volume and microphone gain | Where the chip's audio level sits before the board's amplifier |
| `listen(cb, ctx)` | The event callback (2.2) | Runs on the radio task, from `poll` |
| `caps` | `DMRC_CAP_*` bits | What the chip offers: `SMS`, `RADIO_CHECK`, `REMOTE_MON`, `STUN`, `CALL_ALERT`, `EMERGENCY`, `ALL_CALL`, `RSSI`, `BER`, `ENC_AES128`, `ENC_AES256`, `ENC_ARC`, `HEAR_ALL` |

### 2.2 Events

| Event | Fields | The core does |
|---|---|---|
| `DMRC_EV_READY` / `DMRC_EV_FAULT` | firmware version / reason | Sends the channel, groups and key; on a fault, `dmr.fault` in the view and a retry with power off and on |
| `DMRC_EV_STATE` | idle, receiving, transmitting, transmit refused (busy, no permit, timeout) | `radio_dmr_view_t.state`, the TX screen, the talk permit or busy tone |
| `DMRC_EV_CALL` | start / end, type, source ID, target ID, encrypted, emergency | `view.dmr.call`, `last`, the reply window, the call log (Recent), the RX screen |
| `DMRC_EV_SMS` | source, target, group, text | The inbox (`ui_svc_msg_*`), as for a burst-level SMS |
| `DMRC_EV_SMS_DONE` | sent, failed, no answer | The message's sent state |
| `DMRC_EV_FUNC_RESULT` | function, ID, result | Radio check's answer, as the burst-level stack reports it |
| `DMRC_EV_FUNC_HEARD` | function, source | A stun or monitor request to us: as the chip handles it, shown and logged |
| `DMRC_EV_RSSI`, `DMRC_EV_BER` | dBm, percent | `view.dmr.rssi_dbm`, a `ber` field |

`hal/hal_dmr_call.h` has the rows above up to `key()`, `volume()`, `mic_gain()`, `listen()` and `caps`, plus the key
store (2.4). `sms()`, `function()` and `emergency()` and their events wait for a chip whose command formats are known:
a driver can't offer them yet, so the interface doesn't either. `call()` takes the call type and the ID. `caps` adds
`DMRC_CAP_CALLS` (the chip can start calls; without it the radio core refuses PTT, `tx_deny=rejected`) and
`DMRC_CAP_CALL_INFO` (it reports calls heard and its state; without it a call counts as on air once the chip took
it).

### 2.3 What the core keeps

The core keeps the talk target, focus, reply window, the contacts and names, the inbox, the call log and the key store.
The chip's own contact list, if it has one, is not used: the core resolves names from the codeplug and the ID
database for both backends. A chip that needs a contact entry before it calls a private ID gets it from the driver,
one at a time, not as a copy of the codeplug.

### 2.4 The board's side

| Item | Board |
|---|---|
| RF chip ownership | Where the DMR chip drives the RF chip itself, the board hands the bus over in `power()` and in its FM `rx()` / `tx()`; the FM hooks refuse while the DMR chip owns it |
| Audio switching | Receive audio and the microphone from the DMR chip on a DMR channel, from the RF chip on FM. The DMR chip's audio reaches the speaker only while a call heard plays (the driver tells the board), so its output between calls and while transmitting stays off |
| Signal level | Where the DMR chip owns the RF chip, the board's `status()` gives the driver's level in DMR (the one `DMRC_EV_RSSI` carries) and -160 dBm, nothing heard, without one |
| UART | The board's UART with an interrupt into the driver's receive ring |
| Power pin, reset | As the chip needs |
| Key store | `key_count`, `key_get(number, *key)` and the optional `key_put(*key)` of `hal_dmr_call_t`: where the radio keeps its keys, in its own record format. The board's flash map names the range as `secret_addr` / `secret_size` (section 6) |
| Bus and audio hand-over | In the board's `set_mode()`: the radio core calls `set_mode(DMR)` before it powers the chip and powers it off before `set_mode(FM)` |

## 3. The UI and the codeplug

The UI calls `ui_svc_*`. `core/ui/ui_svc_radio.c` already routes them to the radio core; the radio core sends them to
the burst-level stack or to `dmr_call`. No screen knows which backend runs.

| UI | Service | Call-level backend |
|---|---|---|
| Home on a DMR channel: target, color code, slot, repeater | `radio_dmr_view_t` | From the channel and `DMRC_EV_STATE` |
| RX screen: caller, target, names, encrypted mark, RSSI | `view.dmr.call`, `rssi_dbm` | `DMRC_EV_CALL`, `DMRC_EV_RSSI` |
| PTT and the TX screen | `radio_sm` PTT | `call(target, on)` |
| Contacts: talk to, focus, call back | `ui_svc_talk_to`, `ui_svc_focus_target` | The core's target, sent with the next `call()` |
| Recent | the call log | From `DMRC_EV_CALL` |
| Messages: inbox, send | `ui_svc_msg_*`, `ui_svc_msg_send` | `DMRC_EV_SMS`, `sms()` |
| Radio check, call alert | `ui_svc_radio_check`, `ui_svc_call_alert` | `function()`; shown only with the matching `DMRC_CAP_*` |
| Emergency | `ui_svc_emergency` | `emergency()` |
| Quick tiles: slot, color code | `ui_svc_dmr_timeslot`, `ui_svc_dmr_color_code` | `channel()` again with the temporary value |
| Quick tile: privacy | `ui_svc_privacy` | `key()` (section 6) |
| Quick tile and the digital monitor: hear all | `radio_req_dmr_hear_all`, MONITOR | Only with `DMRC_CAP_HEAR_ALL`: `dmrc_channel_t.hear_all`, sent with `channel()` on every change |
| DMR VFO | the VFO | `channel()` with the typed values |

| Codeplug field | Use on the call-level backend |
|---|---|
| `rx_hz`, `tx_hz`, `power`, `color_code`, `timeslot`, `dmo`, `dmr_id` | `channel()` |
| `contact` (talk target) | The core's target, then `call()` |
| `listen_list` (RX group list) | `groups()` |
| `tot_s` | The core's TX time-out ends the call with `call(.., off)` |
| `alias_tx`, `roam`, `sfr_*` | Not used (section 4) |
| `privacy_slot` (native codeplug, [codeplug-native.md](codeplug-native.md)) | Which key `key()` loads (section 6) |

## 4. Build switches

`CONFIG_DMR` keeps its meaning: the burst-level stack. A new `CONFIG_DMR_CALL` builds the call-level backend
(`core/dmr/call_level/`, the driver in `drivers/dmr/<chip>/`). `core/config.h` derives `CONFIG_DMR_ANY`
(`CONFIG_DMR || CONFIG_DMR_CALL`, not set by hand), and the UI screens, `radio_dmr_view_t` and the codeplug's DMR
fields move from `#if CONFIG_DMR` to `#if CONFIG_DMR_ANY`. Building both is an error.

| Switch | Burst-level radios | Call-level radios |
|---|---|---|
| `DMR` | as today | `BOARD_UNSUPPORTED`: the chip gives no burst access |
| `DMR_CALL` (new) | `BOARD_UNSUPPORTED`: no such chip | the DMR backend |
| `DMR_SMS` | as today | builds with either parent; needs `DMRC_CAP_SMS` |
| `DMR_TMS`, `DMR_ARS`, `DMR_LRRP`, `DMR_GPS` | as today | `BOARD_UNSUPPORTED` unless the chip's documents offer them |
| `DMR_SIGNALING` (radio check, call alert) | as today | builds with either parent; each function needs its `DMRC_CAP_*` |
| `DMR_EMERGENCY` | as today | builds with either parent; needs `DMRC_CAP_EMERGENCY` |
| `DMR_PRIVACY` | as today (basic privacy, our keystream) | not built: it needs `DMR`. The chip's encryption is `DMR_AES` |
| `DMR_AES` (new) | off: it needs `DMR_CALL` | the chip's encryption, with the board's key store (section 6) |
| `DMR_TALKER_ALIAS` | as today | `BOARD_UNSUPPORTED` unless the chip reports aliases |
| `DMR_ROAMING` | as today | `BOARD_NOT_WRITTEN`: could run on `DMRC_EV_RSSI`, not designed |
| `DMR_IP_GATEWAY` and everything under it, `ZONE_SHARE`, `DMR_UDP_COMPRESS`, `SFR*`, `HOTSPOT*` | as today | `BOARD_UNSUPPORTED`: need bursts or packet data |
| `VFO_DMR` | as today | builds with either parent |
| `IDDB_COMPACT` | as today | builds with either parent: names only |
| `SCAN`, `DUAL_WATCH` on DMR channels | as today | `BOARD_NOT_WRITTEN` for DMR channels at first: each step is a `channel()` round trip at the chip's pace. FM channels scan as before |

Built so far: `VFO_DMR` and `CONTACT_EDIT` take either parent. The other sub-features still need `DMR`, so on a
call-level radio they are off (ci's maximal configuration drops them) until the chip's driver offers what they need.
Each sub-feature's "needs DMR" in `config/features.mk` becomes "needs DMR or DMR_CALL" where the table says either
parent. Rows that need a `DMRC_CAP_*` also hide their UI when the chip lacks it (`UI_FEAT_*`).

## 5. Threading

| Item | Rule |
|---|---|
| Where the driver runs | In the radio task, from `radio_sm_poll` (and woken by the UART interrupt through the radio task's queue). No task of its own: no stack to pay for |
| The engine | A plain state machine in `core/dmr/call_level/` and the chip's framing in `drivers/dmr/<chip>/`: bytes in, frames out, timers from `now_ms`. No RTOS call, no wait. Host tests run it against a fake chip |
| The UART interrupt | Puts bytes in a ring, nothing more |
| Pacing | One request in flight, the gap and reply time-outs the chip's protocol needs, retries, all as timers in the engine |
| Requests from the UI | As today: `radio_req_*` posts to the radio task, the radio core calls the hal |
| Events | From `poll`, on the radio task; the radio core updates the view and publishes it to the UI as today |

## 6. Encryption

The chip encrypts and decrypts itself. The core's part is which key the radio uses and getting that key into the
chip. The keys themselves stay in the board's key store (2.4), in the records the radio's own software keeps, so keys
entered with that software work unchanged.

| Item | As built |
|---|---|
| Switch | `CONFIG_DMR_AES`, needs `DMR_CALL` (not `DMR_PRIVACY`: that is the burst-level stack's basic privacy) |
| Key store | The board's: `hal_dmr_call_t.key_count` keys, numbered 1 to n, each a type (`dmrc_key_type_t`: ARC, AES-128, AES-256) and 5, 16 or 32 bytes. Read from flash when needed (`key_get`), never kept in RAM. `key_put` is optional: a board whose records sit in a range it doesn't write leaves it out, and keys are entered with the radio's own software |
| Key in use | One radio-wide choice, `settings_t.dmrc_key`: the key number, bit 7 set while it is off. The Privacy Quick tile turns it on (the number in use, else the first key the store holds; "No encryption key in the radio" when it holds none) or off. The codeplug has no per-channel key yet |
| Loading | On every DMR channel change, after the chip's ready report and after the choice changes: the call-level core reads the key into a stack buffer, calls `key()` (NULL: encryption off), and clears the buffer. The driver keeps its copy until the chip has taken it, then clears that too, and the UART clears each byte it sends |
| The chip's command | The driver sends the key with exactly the parameters the chip's hardware page gives (key number, type byte, the bytes). RadioFoundry defines no key format of its own |
| Entering a key | The debug link's STORAGE op 3 (`DMR_KEY`), write only, behind the chip UID token like REBOOT ([debuglink.md](debuglink.md) 3.13): `rfoundry_debug.py storage dmr-key N TYPE HEX`. `DL_ST_UNAVAILABLE` on a board without `key_put`. Never through the codeplug |
| Keys out of the debug link | The `dmr` section shows `dmr.enc` (`off`, `arc`, `aes128`, `aes256`), `dmr.key_id` and `dmr.key_loaded`, never key bytes. The driver's last-report key prints no data for the key command. The STORAGE reads cover only the settings ring, the journal and the calibration copy |
| Keys out of the CPS | The board's flash map names the key range (`hal_flash_map_t.secret_addr`, `secret_size`): the open-firmware CPS refuses area 1 reads that touch it |
| Interop | Works on stock (owner report); test again with our driver (9) |

## 7. Driver layering

| Layer | Place | Does |
|---|---|---|
| Frame | `drivers/dmr/<chip>/<chip>_frame.c` | The chip's frame: build, checksum, parse from the receive ring. Pure functions, host tested |
| Commands | `drivers/dmr/<chip>/<chip>_cmd.c` | One function per command: arguments to a frame, a reply or report to an event. The chip's start-up sequence |
| Engine | `drivers/dmr/<chip>/<chip>.c` | The request queue, pacing, retries, the ready wait, the `hal_dmr_call_t` operations |
| Core | `core/dmr/call_level/` | `hal_dmr_call_t` events to `radio_dmr_view_t`, the call log, the inbox; channel and key loading; chip-independent |
| Board | `boards/<radio>/` | The UART, the power pin, the RF bus and audio hand-over, `board.dmr_call` |

A second chip of this kind gets its own `drivers/dmr/<chip>/` and the same core.

## 8. Tests

| Test | What |
|---|---|
| Host: the frame layer | Checksums and frames from the chip's documents, round trips, a broken frame refused |
| Host: the engine against a fake chip | Pacing, one request at a time, retries, the ready wait, a time-out fault |
| Host: the call-level core | A call heard (view, names, Recent), PTT to the talk target, an SMS in and out, radio check, a key loaded on channel change and cleared from the buffer, no key bytes in any debug link output |
| `uiFlowTest` | The DMR flows run on the call-level sim backend too |
| HIL | Opt-in with a second DMR radio (the bring-up page): calls both ways, SMS both ways, radio check, encryption with a shared key |

Written so far: `tests/host/hf6853Test.c` (the first chip's frames, commands and engine against a fake chip: the
power-up wait and its retry, the start-up, pacing, retries, give-ups, the lost link, the channel change and key, calls
both ways with PTT up always after a PTT down that went out, a call heard ended by the chip's end event or the long
fallback time-out, the speaker following it, hear all on and off, the signal level polled only while a signal is heard
and never ahead of a PTT, no key bytes in the driver's keys), `tests/host/dmrCallLevelTest.c` (the core against a fake `hal_dmr_call_t`: channel, groups and
key after ready and on change, hear all with the channel, a call heard, its signal level and the reply window, PTT refused without `DMRC_CAP_CALLS` and sent with
it, on the air only after the chip's TX state with `DMRC_CAP_CALL_INFO`, all call, the TX time-out, a fault) and
`tests/hil/test_dmr_call_level.py` (the chip ready on a DMR channel, off on FM, the key taken; opt-in a call from a
second radio shown with its IDs; PTT, radio_tx). SMS, radio check and the sim backend
for `uiFlowTest` wait for the operations in 2.1.

## 9. Open questions

- How a received encrypted call's key number is reported. The key command itself (key number, type, bytes, and type 0
  for off) is on the chip's hardware page.
- AES interop with other radios: works on stock (owner report); test again with our driver.

## 10. What is built

| Part | State |
|---|---|
| `hal/hal_dmr_call.h`, `board_t.dmr_call` | Built: the operations of 2.1 without `sms()`, `function()`, `emergency()` |
| `core/dmr/call_level/` | Built: the call stack's interface (`dmr/call/dmr_call.h`) over the chip, so the radio core's talk target, reply window, focus, names and Recent run unchanged |
| The first chip's driver | Built: power-up, start-up, pacing and retries, the channel, the key, hear all (`DMRC_CAP_HEAR_ALL`), calls both ways (PTT, and the calls heard to the core), the receive audio following the call heard, the signal level (`DMRC_EV_RSSI`, on a scale not yet calibrated). A call heard ends at the chip's end event; a long quiet time-out is only the fallback should that event be lost. SMS, the group list and the chip's other reports wait for their formats (the chip's hardware page, open questions) |
| `CONFIG_DMR_AES` | Built (section 6) |
| On a radio | Nothing run yet: the radio's bring-up page has the steps |
