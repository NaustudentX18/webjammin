# HARDWARE_WIRING — a 4/5-button fret board for the Cardputer Zero

WeBjammin can be played on the onboard 46-key keypad, on a USB or Bluetooth
guitar, or on a DIY button box wired to the **14-pin EXT header** on the top of
the chassis. This sheet covers the DIY box.

> **Verify the pin numbers against your own board before you solder.** The
> table below is from the vendor's pinout for the C154 (full edition) and the
> Debian image we tested on. If your revision differs, confirm with
> `gpioinfo` (libgpiod) or by reading `/sys/class/gpio/gpiochip*/label`, and
> change the numbers in `config.json` — no recompile is needed.

## 1. The EXT header (2.54 mm, 14-pin, dual row)

| Pin | Signal | BCM | Use it for | Note |
|-----|--------|-----|------------|------|
| 1 | HAT_P0 | GPIO26 | *optional* fret | muxed to USB by default — see §5 |
| 2 | 5VIN | – | power in | external 5 V input (charging / host feed) |
| 3 | HAT_P1 | GPIO23 | *optional* fret | muxed to USB by default — see §5 |
| 4 | GND | – | **common ground** | every button returns here |
| 5 | G22 | GPIO22 | fret 1 (default map) | free GPIO |
| 6 | 5VOUT | – | power out | switched 5 V rail, max 500 mA |
| 7 | G11 | GPIO11 | fret 2 (default map) | free GPIO |
| 8 | G2 | GPIO2 | **avoid** | primary I²C SDA (bus has the onboard sensors) |
| 9 | G10 | GPIO10 | fret 3 (default map) | free GPIO |
| 10 | G3 | GPIO3 | **avoid** | primary I²C SCL |
| 11 | G9 | GPIO9 | fret 4 (default map) | free GPIO |
| 12 | G15 | GPIO15 | strum (default map) | UART0 RX — unusable if you need the serial console |
| 13 | G7 | GPIO7 | fret 5 (default map) | free GPIO |
| 14 | G14 | GPIO14 | spare / strum alt | UART0 TX — same caveat as pin 12 |

The default mapping in `config/config.json` uses **5, 7, 9, 11, 13** for the five
frets and **12** for the strum, so nothing on the I²C bus or the UART is
disturbed.

## 2. Wiring

Buttons are **active-low**: one side of the switch to the GPIO pin, the other
side to GND. The internal pull-up is enabled by the driver, so no external
resistor is needed (a 10 kΩ external pull-up is still fine and makes the
wiring more robust against long leads).

```
   3V3 (internal pull-up)
        │
   GPIO22 ───────┐
                  ▭  tact switch
   GPIO11 ───────┤
                  ▭
   GPIO10 ───────┤
                  ▭
   GPIO9  ───────┤
                  ▭
   GPIO7  ───────┤
                  ▭
   GPIO15 ───────┤   (strum)
                  ▭
   GND   ────────┴─────────────  header pin 4
```

- Use **normally-open** momentary switches (tactile or reed).
- Keep the leads short (< 15 cm). Long wires next to the panel driver pick up
  noise and cause phantom fret presses.
- Add a **strain relief** on the cable: a knot or a hot-glue blob at the
  header end. The header is 2.54 mm pitch and the plastic socket is not rated
  for sideways load.
- Leave the board powered from the battery; do not feed 5 V into pin 6 while
  charging from pin 2.

## 3. Configuring the pins

Edit `~/.config/webjammin/config.json` (copy `config/config.json` there on
first run):

```json
"gpio": {
  "enabled": true,
  "active_low": true,
  "debounce_ms": 5,
  "pins": [
    { "bcm": 22, "label": "fret1", "lane": 0 },
    { "bcm": 11, "label": "fret2", "lane": 1 },
    { "bcm": 10, "label": "fret3", "lane": 2 },
    { "bcm": 9,  "label": "fret4", "lane": 3 },
    { "bcm": 7,  "label": "fret5", "lane": 4 },
    { "bcm": 15, "label": "strum", "strum": true }
  ]
}
```

- `bcm` is the **BCM** number (the `GPIO n` label on the header), not the
  sysfs number. The kernel maps BCM → sysfs itself.
- `debounce_ms` 3–8 ms is the sweet spot: fast enough for rhythm play, slow
  enough to reject contact bounce. Raise it to 10 if your switches are noisy.
- A 4-button box simply omits one fret entry (use frets 1–4 and leave fret 5
  unbound); the chart's lane count decides how many lanes are used.

The game prints the GPIO status at start-up:

```
gpio: 6 pins active
```

If it prints `gpio: cannot export GPIO 22: ...`, the pin is already in use by
another driver — pick a different one.

## 4. 5-Cherry MX Switch Mechanical Mod & 3D Printed Bracket

For the ultimate *Pixel Riffs* and *Guitar Hero* arcade experience, you can attach a **5-Cherry MX mechanical switch caddy** directly to the top edge of the Cardputer Zero chassis.

```
       +-------------------------------------------------------+
       |  [Fret 1]   [Fret 2]   [Fret 3]   [Fret 4]   [Fret 5] |  <- 14.0mm MX Cutouts
       |   (Green)    (Red)     (Yellow)    (Blue)    (Orange) |
       +-------------------------------------------------------+
          |                                                 |
     [Left Clip]                                       [Right Clip]
          |                                                 |
   =======+=================================================+=======
          |      Cardputer Top Edge & 14-Pin EXT Header     |
```

### Switch Recommendations
- **Cherry MX Speed Silver (Linear)**: 1.2 mm actuation travel, 45 cN operating force. Offers the lowest latency for fast trills and consecutive strum notes.
- **Cherry MX Red (Linear)**: 2.0 mm actuation travel, 45 cN operating force. Traditional smooth feel.
- **Kailh Choc V1 / V2 (Low Profile)**: Ideal for a slim, compact build that stays flush with the chassis.

### 3D Printable Caddy Specifications
- **Plate Cutout**: Standard Cherry MX cutout is **14.0 mm × 14.0 mm** with a plate thickness of **1.5 mm**.
- **Switch Pitch**: 19.05 mm standard keyboard center-to-center spacing.
- **Header Interface**: 2.54 mm pitch 14-pin female socket glued to the underside of the bracket aligning with the top EXT header.
- **Wiring**: Solder a common ground bus wire across one pin of all 5 switches to Header Pin 4 (GND). Wire the second pin of each switch to Pins 5, 7, 9, 11, and 13 respectively.

## 5. 4-button vs 5-button

- **5 buttons**: frets 1–5 + strum. Full Guitar Hero style play.
- **4 buttons**: frets 1–4 + strum, or 4 frets and no strum (set
  `"strum_required": false` and the strum becomes a no-op).
- The **chart** decides the lane count: `"lane_count": 4` charts only use the
  first four lanes.

## 5. Pins 1 and 3 (HAT_P0 / HAT_P1)

These two are multiplexed with the USB 2.0 data lines through the M5IOE1
(`PYG1_HAT_SW`). With the default mux they are **not** GPIOs; the vendor's
switch position (`0`) maps them to GPIO26/GPIO23. If you want to use them as
frets, put the board in that mode and use BCM 26 and 23 — and do not use the
USB port at the same time.

## 6. USB and Bluetooth guitars

No wiring needed:

- **USB guitar / gamepad** — plug it into the CM0's USB port. It appears as
  `/dev/input/event*` and is picked up within a second, without restarting.
  The default `gamepad-usb-bt` profile maps the five face buttons
  (BTN_SOUTH…BTN_TRIGGER) to the frets and the thumbs to the strum.
- **Bluetooth guitar** — pair it with `bluetoothctl`:

  ```bash
  bluetoothctl
  [bluetooth]# scan on
  [bluetooth]# pair AA:BB:CC:DD:EE:FF
  [bluetooth]# trust AA:BB:CC:DD:EE:FF
  [bluetooth]# connect AA:BB:CC:DD:EE:FF
  ```

  The controller appears as an evdev node and the same profile applies. If
  your guitar sends *axes* instead of buttons (many use triggers for frets),
  add axis bindings to the profile:

  ```json
  { "axis": 2, "axis_value": 16000, "lane": 0 }
  ```

  The in-game **remapper** (title → catalogue → back) can capture a button
  and write it into the profile without editing the file.

## 7. Testing

```bash
./webjammin --selftest          # lists every input device the game can see
```

The calibration screen (title → catalogue → back → calibration) shows a live
view of pressed frets, the backend that produced them (`GPIO`, `USB-HID`,
`BT-HID`, `KEYBD`) and the raw device name.
