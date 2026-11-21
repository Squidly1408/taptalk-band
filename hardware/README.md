# Hardware

See [docs/design.md](../docs/design.md) for the full rationale and [bom.csv](bom.csv) for costs (all estimates until quoted).

## Proof-of-concept build

Off-the-shelf parts, roughly AU$120 in total:

- ESP32-S3 dev board (e.g. ESP32-S3-DevKitC-1, N16R8)
- 1.54 in 200 x 200 black/white e-paper module (SSD1681 controller)
- MAX98357A I2S amplifier breakout + small 8 ohm speaker
- Coin vibration motor + NPN transistor (e.g. 2N2222 or S8050) + 1N4148 flyback diode + 1 k resistor
- 5 tactile buttons (4 symbol buttons + page key)
- LiPo cell (300-400 mAh) + charger board
- Qi receiver coil module + a Qi charging pad
- 3D-printed shell and clear 2 x 2 keypad (resin printer gives the clearest caps)

## Pin map (proof of concept)

Defined in [`firmware/main/pins.h`](../firmware/main/pins.h). Change it there if your board differs.

| Function | ESP32-S3 GPIO |
| --- | --- |
| Button 1 (top left) | 4 |
| Button 2 (top right) | 5 |
| Button 3 (bottom left) | 6 |
| Button 4 (bottom right) | 7 |
| Page key | 15 |
| E-ink SCK | 12 |
| E-ink MOSI (SDA) | 11 |
| E-ink CS | 10 |
| E-ink DC | 9 |
| E-ink RST | 8 |
| E-ink BUSY | 18 |
| I2S BCLK | 13 |
| I2S LRCLK (WS) | 14 |
| I2S DOUT (to amp DIN) | 42 |
| Amp shutdown (SD) | 41 |
| Vibration motor (via transistor) | 40 |
| Charger "power good" (low = on charger) | 16 |
| Battery voltage (via 1:2 divider) | 1 (ADC1 channel 0) |

Buttons and the page key connect between the GPIO and ground (internal pull-ups are used). All buttons, the page key and the charger pin are on RTC-capable GPIOs so they can wake the chip from deep sleep.

## Production board (to do)

- [ ] Schematic in KiCad
- [ ] Flex switch ring design
- [ ] Coil and ferrite placement against the panel and battery
- [ ] Antenna keep-out with the silicone bumper and wrist (test detuning on a wrist phantom)
- [ ] Enclosure CAD (shell, keymat, caps, charging puck, class tray)
