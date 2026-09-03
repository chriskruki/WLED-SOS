# NFC preset tap reader (`nfc_preset`)

Tap an NFC tag against the reader → WLED applies a preset → after a few seconds it falls
back to another preset.

The tags are written as an ordinary **URL** — `https://sacredimagination.co/?c=3` — so they
can be programmed by anyone with a free phone app, and they still open the site if a guest
taps them with a phone. The `?c=` parameter (1–5) picks which preset fires. Everything else
about the URL is ignored: scheme, `www.`, sub-paths and any other query parameters. Only the
host has to match.

Tag detection + URL matching come from the shared **nfc-schema** submodule
(`nfc-schema/`), so the tag format stays in lockstep with the totem firmware. Only the WLED
binding lives here.

| File | Role |
|------|------|
| `usermod_nfc_preset.cpp` | WLED binding: config, PN532 reader task, tap → preset → revert |
| `nfc-schema/` (submodule) | shared reader + `schemas.json` + generated `nfc_wled.h` (host, `?c=` range, preset config) |

---

## 1. Parts

| Part | Notes |
|------|-------|
| ESP32-S3 board | DevKitC-1 or equivalent. Built/tested against `esp32s3dev_16MB_opi`. |
| PN532 NFC module | The common red "V3" breakout (Elechouse-style) or an Adafruit PN532 breakout. Must support **HSU / UART** mode. |
| NTAG213 tags | NTAG215/216 also fine. Stickers, cards or discs. Cheap 25 mm stickers work well. |
| 4 jumper wires | Keep them under ~20 cm. |

Plain **MIFARE Ultralight** works too — the reader stops as soon as the message parses, so
it never runs off the end of a small tag. **MIFARE Classic is not supported**; use NTAG.

---

## 2. Put the PN532 in HSU (UART) mode

This is the single most common setup mistake. The module ships in I2C mode.

The red V3 board has two DIP switches, usually labelled **SET0** and **SET1**:

| Mode | SET0 | SET1 |
|------|------|------|
| **HSU / UART** | **0 (OFF)** | **0 (OFF)** |
| I2C | 1 (ON) | 0 (OFF) |
| SPI | 0 (OFF) | 1 (ON) |

> Confirm against the table silkscreened on the back of your own board — a few clones
> reverse the switch orientation. Power-cycle the module after changing the switches;
> the mode is only read at boot.

Adafruit's PN532 breakout uses solder jumpers (SEL0/SEL1) instead — both open = UART.

---

## 3. Choosing the ESP32-S3 GPIOs

You need two free GPIOs. On an ESP32-S3 these are **not** safe:

| Avoid | Why |
|-------|-----|
| 0, 3, 45, 46 | strapping pins — held levels change boot behaviour |
| 19, 20 | native USB D−/D+ (this build boots with USB CDC on) |
| 26–32 | SPI flash |
| 33–37 | octal PSRAM — unusable on any `_opi` board |
| 43, 44 | UART0, the serial console |
| 48 (or 38) | onboard RGB LED, depending on board revision |

**Safe range: GPIO 4–18 and 21.** This guide uses **GPIO 17** and **GPIO 18**.

Before committing, open WLED's *LED Preferences* and *Config → Settings* and make sure
neither pin is already taken by your LED bus, button, IR or relay. WLED's pin manager will
refuse the allocation if it is, and the Info panel will show the reader as `not found`.

---

## 4. Wiring

The ESP32-S3 is **not 5 V tolerant**. Power the module from **3V3**, not 5V.

| PN532 pin | ESP32-S3 | Notes |
|-----------|----------|-------|
| `VCC` | `3V3` | ~110 mA peak while the RF field is on |
| `GND` | `GND` | must be common with the ESP32 ground |
| `TXD` (silkscreened `SDA`) | **GPIO 17** — ESP RX | module talks → ESP listens |
| `RXD` (silkscreened `SCL`) | **GPIO 18** — ESP TX | ESP talks → module listens |

```
   ESP32-S3                        PN532 (HSU mode)
  ┌──────────┐                    ┌──────────────────┐
  │     3V3  ├────────────────────┤ VCC              │
  │     GND  ├────────────────────┤ GND        ╭───╮ │
  │ GPIO 17  ├◄───────────────────┤ TXD / SDA  │ 🌀│ │  antenna
  │ GPIO 18  ├────────────────────► RXD / SCL  ╰───╯ │
  └──────────┘                    └──────────────────┘
```

> The 4-pin header on the V3 board is silkscreened for I2C (`SDA`/`SCL`); in HSU mode those
> same two pins carry the UART pair. Labelling varies between clones. **If the reader comes
> up as `not found`, swap those two wires** — it is harmless and it is by far the most common
> cause. The rule that always holds: the module's transmit pin goes to the ESP's receive pin.

### Placement

- Read range is **2–3 cm** with NTAG213. Mount the antenna behind non-metallic material no
  more than ~5 mm thick. Acrylic, wood and fabric are fine; metal or foil kills it entirely.
- Keep the antenna **at least 5 cm from the LED strip** and from any metal frame. LED strips
  are electrically noisy and will shorten the read range.
- Route the four jumper wires **away from and not parallel to** the LED data line.
- Mark the tap spot on the enclosure — people can't find an invisible reader.

---

## 5. Build and flash

Create `platformio_override.ini` in the repo root (it is gitignored):

```ini
[env:nfc_esp32s3]
extends = env:esp32s3dev_16MB_opi
custom_usermods = nfc_preset
```

```sh
pio run -e nfc_esp32s3                 # build
pio run -e nfc_esp32s3 -t upload       # flash
```

Adjust `extends` if your board differs (`esp32s3dev_8MB_opi`, `esp32s3_4M_qspi`, …).
The `Adafruit PN532` library is pulled in automatically by `library.json`.

To run the URL-matching tests on your machine (no hardware needed):

```sh
make -C usermods/nfc_preset/nfc-schema/test
```

---

## 6. Save your presets first

In the WLED UI, create the presets you want to trigger and note their slot numbers — for
example 1–5 for the five colours plus 6 for the idle look the piece returns to.

---

## 7. WLED settings

*Config → Usermods → **NFC***

| Setting | Meaning | Suggested |
|---------|---------|-----------|
| `enabled` | master switch | on |
| `pin[0]` | ESP RX ← PN532 TX | 17 |
| `pin[1]` | ESP TX → PN532 RX | 18 |
| `presets[0..4]` | preset applied for `?c=1` … `?c=5` | 1, 2, 3, 4, 5 |
| `revert` | preset to return to. **0 = stay on the tapped preset** | 6 |
| `hold` | seconds before reverting | 5 |
| `poll` | ms between polls. Lower = snappier taps, more power | 200 |

Changing pins or toggling `enabled` re-initialises the reader immediately. All other settings
take effect on the next tap.

There is no tag-size setting: the reader reads a page at a time and stops the moment the NDEF
message parses, up to a 64-byte cap (`NFC_MAX_TAG_BYTES`). That covers a URL of roughly 55
characters on any tag. If the client needs longer sub-paths, raise the constant.

---

## 8. Writing the tags

This is the whole instruction set for whoever programs them:

> 1. Install **NFC Tools** (free, iOS and Android).
> 2. *Write* → *Add a record* → **URL/URI**.
> 3. Type `https://sacredimagination.co/?c=3` — change the number at the end to pick the
>    colour, 1 through 5. Leave `?c=3` off entirely for the default.
> 4. *Write* → hold the phone against the tag.

Any of these are accepted as the same trigger:

```
https://sacredimagination.co                 https://www.sacredimagination.co/?c=2
https://sacredimagination.co/                http://sacredimagination.co/shop
HTTPS://SacredImagination.CO/?c=3            https://sacredimagination.co/a/b?utm=x&c=5
```

Tags for any other host are ignored (they show up in the Info panel as *NFC unknown tags*).

To point the tags at a different site later, change the `url-krayon` host in the shared
`nfc-schema/schemas.json`, regenerate, and reflash — the tags themselves don't need rewriting
if the host stays the same.

---

## 9. What happens on a tap

1. The reader task (its own FreeRTOS task on core 0, so it never stalls the LED render loop)
   polls for a tag every `poll` ms. The PN532 is probed inside that task too, so a missing or
   slow reader can't add to WLED's boot time.
2. On a hit it reads the tag a page at a time, re-parsing after each page and stopping as
   soon as the URL decodes, then pushes `c` onto a queue. `applyPreset()` is called from
   WLED's main loop, never from the reader task.
3. The tapped preset is applied and a `hold`-second timer starts.
4. When it expires, `revert` is applied.
5. Holding the tag against the reader does **not** retrigger — the policy fires once per tap
   and re-arms when the tag leaves. A fresh tap during the window restarts the timer.
6. If you change the light from the WLED app during the window, the pending revert is
   dropped rather than stomping on you.

---

## 10. Troubleshooting

Check *Info* in the WLED UI first — the usermod reports there.

| Symptom | Cause |
|---------|-------|
| `NFC reader: not found` | Wrong DIP mode (§2), RX/TX swapped (§4), no 3V3, wrong GPIO, or the pin is already claimed by another WLED function |
| Reader found, tap does nothing, no counters move | Tag too far, tag not NDEF-formatted, or it's a MIFARE Classic (not supported — use NTAG) |
| `NFC unknown tags` climbing | The tag's URL host isn't `sacredimagination.co`, it isn't a URL record, or the URL is longer than the 64-byte read cap |
| Works only when touching, not at 1–2 cm | Metal or LED strip too close to the antenna |
| Preset fires but never reverts | `revert` is 0, or `hold` is 0 |
| Intermittent drops | UART wires too long or running alongside the LED data line |

Build with `-D WLED_DEBUG` for `NFC: PN532 not found` / `NFC: pin allocation failed` on the
serial console.

---

## 11. State of testing

Verified here:

- Firmware compiles and links clean for `esp32s3dev_16MB_opi` with the usermod enabled.
- `nfc-schema/test/` — native cases through the real NDEF byte path: prefix codes 0x00–0x04,
  `www.`, mixed case, sub-paths, `?c=` in any position, out-of-range and malformed `c`,
  rejection of other hosts, `sacredimagination.company`, the host appearing in a path, a
  non-URI TNF, and the incremental read (every truncated prefix must fail, the first complete
  one must succeed).

Needs bench time on the actual hardware:

- PN532 communication over HSU, and which way round the two data wires go on your board.
- Read range and antenna placement in the finished enclosure.
- The reader task's stack is 3072 bytes, chosen not measured. Check it once with
  `uxTaskGetStackHighWaterMark(NULL)` and trim if there is headroom.

The PN532 sleep/wake path is not implemented: the HSU wake mask is unconfirmed on real
hardware (open question 3 in the totem NFC spec) and a reader that fails to wake is a dead
feature. The reader stays awake between polls.

There is one known efficiency opportunity left. The NTAG `READ` command returns 16 bytes per
exchange, but `Adafruit_PN532::mifareultralight_ReadPage()` discards 12 of them, so a tag read
costs ~4× more UART round trips than it needs to. Fixing it means driving `inDataExchange()`
directly — but that sends the private `_inListedTag`, which only `inListPassiveTarget()` sets,
never the `readPassiveTargetID()` used for detection here. Switching detection would cost the
tag UID used for tap debouncing, so it is left alone until someone can test it on hardware.
