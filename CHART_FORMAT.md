# CHART_FORMAT — WeBjammin chart and catalogue formats

## 1. The native chart: `.wj.json`

A song folder may contain one file named `song.json`, or the chart may be
named `<something>.wj.json`. The song library scans
`~/webjammin/songs`, `/opt/webjammin/songs` and `./songs` (see
`songs.directories` in the config) for these names.

```json
{
  "format": "webjammin-chart",
  "version": 1,
  "title": "Neon Fret",
  "artist": "WeBjammin Synth",
  "charter": "You",
  "difficulty": "Easy | Normal | Hard",
  "lane_count": 5,
  "offset_ms": 0,
  "bpm": 128.0,
  "audio_file": "",
  "synth_patch": "guitar",
  "synth_seed": 1337,
  "tempos": [
    { "time_ms": 0, "bpm": 128.0 }
  ],
  "notes": [
    { "time_ms": 500, "lane": 0, "duration_ms": 0, "hopo": false, "midi": 60 }
  ]
}
```

| Field | Type | Meaning |
|---|---|---|
| `format` | string | `"webjammin-chart"` (informational) |
| `version` | int | format version, currently 1 |
| `title`, `artist`, `charter` | string | shown in the song browser and the results screen |
| `difficulty` | string | free text, shown in the browser |
| `lane_count` | int | 1–5; how many fret lanes this chart uses |
| `offset_ms` | int | per-song correction added to `timing.offset_ms` |
| `bpm` | number | default tempo, used when `tempos` is empty |
| `audio_file` | string | WAV file to stream; empty means "synthesise the song" |
| `synth_patch` | string | timbre selector for the synthesiser |
| `synth_seed` | int | seeds the drum pattern; same seed = same drums |
| `tempos` | array | tempo map: `time_ms` + `bpm`, applied in order |
| `notes` | array | the notes, sorted by `time_ms` |

### Note fields

| Field | Type | Meaning |
|---|---|---|
| `time_ms` | int | when the note must be struck, in milliseconds from the start of the song |
| `lane` | int | fret lane, **0–4** (0 = fret 1). Out-of-range lanes are rejected at load time |
| `duration_ms` | int | sustain length; `0` means a tap |
| `hopo` | bool | the note must be struck with a hop-on (fret **and** strum) |
| `midi` | int | pitch for the synthesised backing track (60 = middle C) |

Notes are sorted by `time_ms` when loaded, so the file does not have to be
ordered.

## 2. The catalogue index

The catalogue is a JSON document fetched from `catalogue.index_url`:

```json
{
  "format": "webjammin-catalogue",
  "songs": [
    {
      "id": "song1",
      "title": "Song One",
      "artist": "Artist",
      "charter": "Charter",
      "difficulty": "Hard",
      "lane_count": 5,
      "chart_url": "https://host/charts/song1.chart",
      "audio_url": "https://host/audio/song1.ogg",
      "license": "CC-BY-4.0",
      "notes": "Optional free-text note",
      "format": "chart"
    }
  ]
}
```

- `songs` may also be called `items` or `entries`, or the root may be a bare
  array.
- The chart URL may be spelled `chart_url`, `download_url`,
  `browser_download_url` or `url` — the GitHub release shape works as-is.
- `audio_url` is optional. When it is missing and the download is a `.zip`,
  the audio inside the archive is used and the chart's `[Music]` entry is
  rewritten to point at it.
- Downloads happen **one song at a time**, when the player presses `ENTER` in
  the catalogue tab of the song browser.

### Serving a catalogue

`scripts/catalogue_server.py` turns a directory of charts into an index and
serves it over HTTP:

```bash
python3 scripts/catalogue_server.py --charts ~/webjammin/songs --port 8000
```

Then point the device at it:

```json
"catalogue": { "index_url": "http://<host>:8000/catalogue.json" }
```

## 3. Imported formats

### Clone Hero `.chart`

The `[SyncTrack]` tempo map and the `[Note]` sections of the **guitar** track
are read; other instruments are ignored. Sustain is converted from ticks to
milliseconds. The tick rate is the Clone Hero standard of 192 ticks per beat.

### Standard MIDI (`.mid`)

Format 0 and 1 files are read. Tempo changes and time signatures are honoured,
note-on/note-off pairs become notes, and percussion (channel 10) is skipped.
Pitches are folded onto the fret lanes by rank, so a piano roll becomes
playable on four or five frets.

### WAV audio

16-bit PCM only (the format the generator writes and the one `ffmpeg -i song
.ogg -ac 2 -ar 48000 -c:a pcm_s16le song.wav` produces). Files are streamed
in small blocks; a long track never sits in RAM.
