# WeBjammin

**A keyboard-driven pixel rhythm game for the M5Stack Cardputer Zero.**

Five frets, one strum, a 320 × 170 pixel highway and songs that are
synthesised from their own charts — so the note you hear and the note you
press are the same event, and nothing drifts over a four-minute song.

Built for the Cardputer Zero's real constraints: a 1 GHz quad-core Cortex-A53
with **512 MB of RAM**, a **320 × 170** panel, a **46-key keypad**, and one
ALSA codec (ES8389).

```
   ┌──────────────────────────────────────────────┐
   │  12,340                    x4          97.2%  │  ← score / combo / accuracy
   │  ████████████████████░░░░░░░░░░░░░░░░░░░░  │  ← jam meter
   │      ┌────┬────┬────┬────┬────┐              │
   │      │ ▓▓ │ ▓▓ │    │ ▓▓ │    │              │  ← the highway
   │      │ ▓▓ │    │    │    │ ▓▓ │              │
   │ ═════╧════╧════╧════╧════╧════  hit line   │
   │      [1]  [2]  [3]  [4]  [5]                 │  ← the keys under the panel
   └──────────────────────────────────────────────┘
```

---

## 1. What it does

| | |
|---|---|
| **Gameplay** | 4- or 5-lane note highway, sustain tails, HOPO notes, tap / hopo / strum rules, adjustable scroll speed and calibration offset |
| **Judgement** | PERFECT ±25 ms, GREAT ±55 ms, GOOD ±90 ms, MISS beyond; score, combo, multiplier (1×–4×), accuracy %, Rock/Jam meter, optional no-fail |
| **Input** | Onboard 46-key keypad, **GPIO buttons** on the 14-pin EXT header, **USB** HID guitars/gamepads, **Bluetooth** HID controllers — all through one input manager, with hot-plug |
| **Songs** | `.wj.json` (native), Clone Hero **`.chart`**, Standard MIDI **`.mid`**, streamed WAV audio, and a **built-in procedural generator** so the game is playable with zero downloads |
| **Catalogue** | Fetch a song **directly from a catalogue website** — point the config at an index URL and download individual songs into the game from the song browser |
| **Extras** | On-screen input tester + calibration, per-song best scores (atomic JSON), settings, offline screen renderer for screenshots |

## 2. Build

Nothing exotic is needed — the device has `gcc`, `make` and `libasound2-dev`.

```bash
git clone <this repo> webjammin && cd webjammin
make                       # → build/webjammin
make test                  # → 20 unit tests
./build/webjammin --generate-songs songs   # write the three bundled songs
./build/webjammin --selftest               # diagnostics
./build/webjammin                         # play
```

On the Cardputer Zero itself (Debian 13 trixie, arm64):

```bash
scp -r . fogo@192.168.4.11:/tmp/webjammin
ssh fogo@192.168.4.11 'cd /tmp/webjammin && make && ./build/webjammin --generate-songs ~/webjammin/songs'
```

Dependencies: `g++` (C++20), `make`, `libasound2-dev`. `curl` and `unzip` are
used for catalogue downloads and are present on the device.

## 3. Controls

### Onboard keypad (works out of the box)

| Key | Action |
|---|---|
| `1` `2` `3` `4` `5` | Frets 1–5 |
| `SPACE` | Strum |
| `F` `X` `Z` `C` | Up / down / left / right (the vendor's arrow keys) |
| `ENTER` | Confirm / start |
| `ESC` | Back / quit to the song list |
| `TAB` | Pause |

### GPIO buttons, USB and Bluetooth

See **[HARDWARE_WIRING.md](HARDWARE_WIRING.md)** — the EXT header pinout, the
active-low wiring, the default pin map, Bluetooth pairing and the axis
bindings for guitars that use triggers instead of buttons.

Every binding lives in `~/.config/webjammin/config.json`, so a new guitar or
button box needs **no recompile**.

## 4. Configuration

Copy the shipped defaults and edit:

```bash
mkdir -p ~/.config/webjammin
cp config/config.json ~/.config/webjammin/config.json
```

| Section | What it holds |
|---|---|
| `audio` | ALSA device, sample rate, period size, volumes |
| `timing` | judgement windows and the calibration offset |
| `gameplay` | scroll speed, no-fail, HOPO support, overstrum penalty |
| `catalogue` | index URL and download folder |
| `gpio` | pin map (BCM numbers), active-low, debounce |
| `songs` | song directories to scan |
| `profiles` | controller profiles: key codes / buttons / axes → frets, strum, menu |

### The audio device matters

The Cardputer Zero's codec is **ALSA card 1**. `pcm.!default` is PipeWire's
and will be busy, so the game uses:

```
plughw:CARD=ES8389Audio,DEV=0
```

## 5. Charts and songs

### Native format (`.wj.json`)

```json
{
  "format": "webjammin-chart",
  "title": "Neon Fret",
  "artist": "WeBjammin Synth",
  "charter": "You",
  "difficulty": "Normal",
  "lane_count": 5,
  "offset_ms": 0,
  "bpm": 128,
  "audio_file": "",
  "synth_patch": "guitar",
  "synth_seed": 1337,
  "tempos": [ { "time_ms": 0, "bpm": 128 } ],
  "notes": [
    { "time_ms": 500, "lane": 0, "duration_ms": 0, "hopo": false, "midi": 60 }
  ]
}
```

- `audio_file` empty → the song is **synthesised** from the chart (lead voice
  per note, seeded drum pattern underneath).
- `audio_file` set → a WAV file next to the chart is streamed instead.
- `synth_seed` drives the drum pattern, so two songs at the same tempo do not
  sound alike.

Full field reference: **[CHART_FORMAT.md](CHART_FORMAT.md)**.

### Importing

| Format | Support |
|---|---|
| `.wj.json` / `song.json` | native, full fidelity |
| `.chart` (Clone Hero / YARG) | guitar track imported, sustain preserved |
| `.mid` (Standard MIDI) | notes extracted, pitches folded onto the fret lanes, percussion skipped |
| `.wav` | streamed as the backing track (16-bit PCM) |

### The catalogue — download songs straight from a website

The song browser has two tabs: **SONGS** (local) and **CATALOGUE**. In the
catalogue tab, `ENTER` downloads the highlighted song: the chart, and the
audio when the entry provides one, are fetched straight from the catalogue
into your song folder, and the song becomes playable immediately.

Set the catalogue in the config:

```json
"catalogue": {
  "enabled": true,
  "index_url": "https://your.host/webjammin/catalogue.json",
  "download_dir": "~/webjammin/songs"
}
```

The index is a small JSON document:

```json
{ "format": "webjammin-catalogue",
  "songs": [
    { "id": "song1", "title": "Song One", "artist": "Artist",
      "charter": "Charter", "difficulty": "Hard", "lane_count": 5,
      "chart_url": "https://your.host/charts/song1.chart",
      "audio_url": "https://your.host/audio/song1.ogg",
      "license": "CC-BY-4.0" }
  ] }
```

Entries may also use the GitHub release shape (`download_url` /
`browser_download_url`). A `.zip` archive is unpacked automatically and the
chart inside is picked up. `scripts/catalogue_server.py` turns any directory of
charts into such an index, so hosting one is a one-liner.

**Rights:** the game downloads exactly what you point it at, one song at a
time, on request — it ships no third-party charts or audio and contains no
bulk scraper. You are responsible for holding the rights to the songs you
fetch; many chart communities distribute charts for songs the user already
owns.

## 6. Calibration

Audio, video and input never line up perfectly — on the Zero, the panel lags
the codec by a few milliseconds and the keypad adds more. The calibration
screen (title → catalogue → back → calibration) plays a steady click and
records your taps; the median error becomes `timing.offset_ms` in the config.

The same offset is used by the judgement code, so it fixes both the audio and
the highway.

## 7. Architecture

```
src/
├── util/      json (hand-written), filesystem helpers, monotonic time
├── chart/     chart model, .wj.json, .chart import, .mid import,
│              procedural generator, song library + metadata cache
├── audio/     ALSA engine (ring buffer + writer thread), chart synth,
│              WAV streaming, song player (the audio clock lives here)
├── input/     evdev backend (USB/BT/keypad, hot-plug), GPIO backend
│              (sysfs, edge + debounce), controller profiles, manager
├── render/    framebuffer (pwrite), 5x7 font, primitives, screens
├── game/      judgement windows, scoring, state machine + gameplay loop
└── app/       config, catalogue client, application shell
```

### Why this stack

| Choice | Reason |
|---|---|
| **C++20** | Deterministic frame pacing, no GC pauses, low RSS, direct access to Linux input and framebuffer APIs. |
| **No SDL2** | SDL2 is not installed on the device, and its DRM/framebuffer backends would fight the APPLaunch launcher for `/dev/fb0`. Writing the panel directly is one `pwrite()` of 108 800 bytes. |
| **No libgpiod** | The kernel already exposes `/sys/class/gpio`; using it means no extra package on the device. |
| **ALSA directly** | We need the exact playback position (`snd_pcm_delay`) to drive note timing — a game framework would hide it. |
| **5×7 bitmap font** | No freetype, no font files, no parsing: ~50 glyphs is all a 320×170 panel needs. |
| **Audio clock as master clock** | Notes are scheduled against frames actually played, so nothing drifts. |

### Memory discipline

- Songs are **streamed**: a four-minute track is never decoded into RAM.
- The synth renders 256-frame blocks on demand.
- The ring buffer is two seconds of audio (~380 KB).
- The frame buffer is 108 800 bytes; the renderer allocates nothing per frame.
- Measured RSS on the device: see `scripts/bench.sh`.

## 8. Testing

```bash
make test
```

20 unit tests cover the JSON parser, the chart readers (native, `.chart`,
`.mid`), the procedural generator's determinism, the judgement windows, the
scoring rules, the synthesiser, the controller-profile translation (buttons and
axes), the font, the atomic file writes, the song library cache and the
catalogue client (including a real download through a local `file://` source).

```bash
./build/webjammin --selftest        # devices, songs, a ten-frame loop test
./build/webjammin --render title out.ppm   # render any screen offline
scripts/bench.sh                     # frame time and RSS on the device
```

## 9. Licence and origin

Original work. The chart formats are implemented for interoperability; no
assets, audio or chart content from any existing commercial game are
included or redistributed. See §5 for the catalogue's rights note.
