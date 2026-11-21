# TapTalk Band: design document

TapTalk Band is a rugged wrist-worn communication button for non-verbal children. Four large clear buttons sit over **one shared e-ink panel**, so each button shows a picture symbol that teachers can change over Bluetooth. The case has no openings at all: it charges wirelessly and is fully sealed. Target school price is about **AU$75**, with a built cost of about **US$26 at 1,000 units**.

The single biggest cost decision is one small e-ink panel under a clear keypad instead of a separate screen under every button. Per-button screens multiply the cost of the most expensive part and add fragile connectors. To the child it looks the same: each clear button has its own picture beneath it.

## 1. Design goals (in priority order)

1. **Safe**: no small parts, no coin cells, nothing sharp, skin-safe materials, survives chewing.
2. **Nearly indestructible**: drops on concrete, being thrown, sand, water, sunscreen, being stood on.
3. **Cheap and replaceable**: low parts cost, replaceable strap, nothing a child can open.
4. **Simple for kids**: big targets, instant feedback (voice plus vibration), works with a fist or palm.
5. **Easy for staff**: connect over Bluetooth, drag symbols onto buttons, record a voice, send. No account.

## 2. Users and requirements

| User | Needs | Design response |
| --- | --- | --- |
| Child (about 4-12, non-verbal or minimally verbal) | Say core words fast during play | 4 buttons per page, 17 mm caps; voice + vibration on press |
| Child with sensory or motor needs | Tolerate wearing it; press with a fist; not startled | Soft silicone; volume control and vibrate-only; ~150 g press force; optional 2-big-button layout |
| Teacher / aide | Set up a child in under 5 minutes; change pages per activity | Drag-and-drop app; templates; Bluetooth from any staff device |
| Speech pathologist | Match the child's symbol system and core vocabulary | Import own images; consistent button positions across pages |
| School IT / business manager | Cheap, no accounts, no data concerns | Offline; no cloud; pairing required |

**Page system:** up to 8 pages of 4 buttons. A recessed side key changes page (long press). Staff can lock a page.

## 3. Hardware architecture

```
                      Staff app (any device)
                               ^
                               | Bluetooth LE
                               v
 Buttons x4 + page key ---> [ ESP32-S3 + 16 MB flash ] ---> 1.54 in e-ink 200x200 (SPI)
                               ^                       ---> I2S amp + IP68 speaker
 Wireless charging coil ---+   | power                 ---> Vibration motor
   (Qi-compatible, 2.5 W)  |   |
                           +-> Charger (ship mode) + 350 mAh LiPo
```

| Block | Part choice | Why |
| --- | --- | --- |
| Microcontroller | Pre-certified ESP32-S3 module, 16 MB flash | Bluetooth LE 5, memory for 32 clips and images, ~US$2-3.50 |
| Display | 1.54 in black/white e-paper, 200 x 200, partial refresh (SSD1681-class controller) | Sunlight readable, zero hold power |
| Buttons | Sealed metal-dome switches on a flex ring around the display edge | Switches never block the picture |
| Audio | I2S class-D amp (MAX98357A-class) + IP68 micro speaker | Diaphragm is the water barrier |
| Haptics | Coin vibration motor via transistor | Feedback for noise-sensitive children |
| Power | 350 mAh protected LiPo; charger IC with ship mode | Sealed band has no off switch, ships disconnected |
| Wireless charging | Qi-compatible receiver + 20 mm coil + ferrite, ~2.5 W | Puck, class tray or any Qi pad |

**Battery life (estimate):** about 2-3 weeks at 200 presses per day. Charge every Friday.

## 4. Buttons and e-ink

The panel is split into four 100 x 100 px tiles. Layer stack, top to bottom:

1. Clear hard-coated polycarbonate caps, slightly domed.
2. Clear silicone keymat sealing the face; each key has a skirt reaching past the window edge.
3. Flex switch ring with one dome switch under each skirt.
4. Fixed 1 mm clear polycarbonate window protecting the e-ink glass.
5. E-ink panel on foam over the main board.

A press inverts its tile for ~0.5 s (partial refresh) while the band speaks and vibrates. Page changes use a full refresh (~2 s), which also clears ghosting. The panel is black and white; colour category cues come from border patterns in the artwork or optional staff-fitted coloured cap rings.

### Default button set

| Page | 1 | 2 | 3 | 4 |
| --- | --- | --- | --- | --- |
| Core | Help | Stop | My turn | Toilet |
| Play | Go | Again | Want | Finished |
| Feelings | Happy | Sad | Angry | Hurt |
| Needs | Drink | Eat | Break | Hug |
| Answer | Yes | No | Don't know | More |

Use openly licensed symbol sets that allow commercial use (for example Mulberry Symbols or OpenMoji) for the shipped artwork. Schools can import their own licensed symbols.

## 5. Enclosure, band and ruggedisation

The device is **completely sealed**. There is no port, charging contact, battery door or screw.

- **Inner shell:** glass-filled nylon or PC/ABS, ultrasonically welded into one piece.
- **Encapsulation:** circuit board potted in soft silicone gel; battery in its own foam pocket.
- **Outer bumper:** food-contact silicone, ~Shore 50A, 2 mm, overmoulded.
- **Raised rim:** 1.5 mm above the caps on all sides.
- **Charging window:** flat 1.2 mm back section over the coil, with alignment magnets.
- **Speaker:** IP68 sealed speaker welded in; grille holes reach only its diaphragm.
- **No small parts** reachable by a child; anything removable is staff-fitted and tested.
- **End of life:** replaced, not repaired; offer a battery take-back.

| Test | Target |
| --- | --- |
| Drop | 26 drops from 1.5 m onto concrete |
| Crush | 80 kg on the face |
| Water and dust | IP68 (1.5 m, 1 hour); rinse under a tap |
| Chew | No pieces detach |
| Strap pull | Band releases before injuring a wrist |
| Chemicals | Sunscreen, sanitiser, hospital wipes |

### Band

Soft woven elastic with a hook-and-loop closure. The hook side folds under a fabric flap. The band threads through two moulded slots (no spring bars).

| Option | Fits wrists | For |
| --- | --- | --- |
| Elastic hook-and-loop, Small | 110-155 mm | Younger children |
| Elastic hook-and-loop, Large | 140-200 mm | Older students, staff |
| Slip-on stretch loop (S/M/L) | Over the hand | Children who remove Velcro |

## 6. Companion app

Flutter app for iOS, Android, Windows and macOS, plus a Web Bluetooth build for Chromebooks (Chrome/Edge). See [ble-protocol.md](ble-protocol.md) and [board-format.md](board-format.md).

**Pairing:** place the band on its charger, hold the page key 5 s, type the 6-digit code shown on the band into the app (LE Secure Connections, passkey). Up to 8 staff devices per band. Pairing is refused when the band is off its charger.

**Workflow:** pick a band (it beeps and flashes), pick a page, tap a tile, choose a symbol or photo, type a label, record a voice or choose a TTS voice, tap Send. Only changed files are sent.

**Features:** drag-and-drop 2 x 2 editor, 8 pages, 2-big-button layout, symbol library, templates, class-wide update on the charging tray, profile export, device settings, battery and last-seen, optional on-device press counts (off by default), Bluetooth firmware updates with roll-back.

**Privacy:** names, photos and voice clips stay on the band and staff device. No cloud.

## 7. Firmware

ESP-IDF with NimBLE. Modes: shipping, asleep, press, page key, Bluetooth window (on charger, or 2 minutes after a 3 s page-key hold), receiving a board (staged, checksummed, then committed), flat battery ("Please charge me" picture), and recovery (watchdog; 15 s page-key hold restarts; page key + button 1 for 10 s on the charger factory-resets).

## 8. Cost (estimates, 1,000 units, US$)

See [hardware/bom.csv](../hardware/bom.csv). Total about **26.10** per band. Chargers sold separately: puck ~3.00, 10-band tray ~25.00. Tooling ~25-40k; certification ~20-30k.

## 9. Compliance

| Area | Standard |
| --- | --- |
| Bluetooth and EMC | ACMA, RCM mark (use a pre-certified module) |
| Wireless charging | ACMA EMC; WPC certification before using the Qi name/logo |
| Child safety | AS/NZS ISO 8124.1, 8124.3 |
| Materials | RoHS, food-contact silicone |
| Battery | IEC 62133-2, UN 38.3 |
| Button batteries | Avoided (sealed LiPo) |
| Medical device | Ask the TGA early; open question |
| Privacy | Australian Privacy Principles |

## 10. Risks

| Risk | Mitigation |
| --- | --- |
| Buttons too small for some children | 2-big-button layout; test in pilot |
| E-ink rated to ~50 C; hot sun or cars | Wide-temperature panel, light colours, shade guidance |
| Sealed band can't be reset with a pin or repaired | Watchdog, key-hold restart, factory-reset gesture, A/B firmware |
| Wireless charging warmth and speed | ~2.5 W cap, temperature cut-off, overnight tray |
| Many bands in one room | Beep/flash on select, names and colours, pairing only on charger |
| Sealing muffles the speaker | Prototype early, measure in a playground |
| Single e-ink supplier | Qualify two with the same footprint |
| Name clash ("TapToTalk" exists) and TGA status | Trademark search; TGA advice |

## Sources

- [Waveshare e-paper specifications](https://www.waveshare.com/product/displays/e-paper/1.02inch-e-paper.htm)
- [LCSC ESP32-S3 pricing](https://www.lcsc.com/product-image/C2913192.html)
- [Liliputing: single e-ink panel under transparent keys (Sonder)](https://liliputing.com/sonder-keyboard-a-customizable-keyboard-with-e-ink-keys-could-eventually-sell-for-199/)
- [ACCC: button battery standards](https://www.accc.gov.au/media-release/accc-welcomes-safety-and-information-standards-for-button-batteries)
