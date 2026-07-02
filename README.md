# hearGOD

Real-time binaural renderer for macOS. Replaces matrix downmix with HRIR convolution using any SOFA-format dataset.

## Download

**[hearGOD v0.3.0 — DMG](https://github.com/jajajou1778/hearGOD/releases/latest)**

Requires Apple Silicon (M1 or later). Intel not supported.

**7.1.4 Atmos bed → stereo binaural over IEMs.**

```
BlackHole 16ch → hearGOD → IEM output
  multichannel    HRIR convolution    binaural
  source          per channel         stereo
                  + target EQ
```

## Features

- 7.1.4 channel layout (FL FR FC LFE BL BR SL SR TFL TFR TBL TBR)
- SOFA HRIR support — plug any public dataset
- Parametric EQ via Equalizer APO / AutoEQ presets
- 4th-order Butterworth LPF for LFE bypass @ 120 Hz
- 256-frame buffer (5.33 ms latency @ 48 kHz)
- NUOLS convolution — uniform CPU load, no scheduling spikes
- Live TUI: CPU load, xrun counter, gain, layout

## Requirements

- macOS 13.0+ — **Apple Silicon (M1 or later) only. Intel not supported.**
- [BlackHole 16ch](https://existential.audio/blackhole/) — multichannel virtual audio device
- A SOFA HRIR dataset (see [HRIR Datasets](#hrir-datasets))

## Build

```bash
# Dependencies
brew install portaudio libmysofa googletest pkg-config

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Test
./build/hearGOD_tests
```

## Usage

```bash
# 7.1.4 Atmos (default 12ch)
./build/hearGOD \
  --sofa ~/Downloads/FABIAN.sofa \
  --eq  ~/eq/moondrop_aria.txt \
  --in  2 \
  --out 1

# 7.1 bed only (8ch)
./build/hearGOD \
  --sofa ~/Downloads/FABIAN.sofa \
  --channels 8 \
  --in 2 --out 1

# Save config
./build/hearGOD --sofa ~/... --in 2 --out 1 --save-config

# List audio devices
./build/hearGOD --list-devices
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--sofa <path>` | required | SOFA HRIR file |
| `--eq <path>` | none | Equalizer APO / AutoEQ preset |
| `--in <N>` | system default | Input device index |
| `--out <N>` | system default | Output device index |
| `--channels <N>` | 12 | Input channel count (12=7.1.4, 8=7.1) |
| `--master-gain <dB>` | 0 | Output gain |
| `--lfe-gain <dB>` | 0 | LFE channel gain |
| `--config <path>` | `~/.config/hearGOD/config.json` | Config file |
| `--save-config` | — | Save current args to config and exit |
| `--list-devices` | — | Print audio device list and exit |

## EQ Format

Equalizer APO / AutoEQ `.txt` format:

```
Preamp: -6.1 dB
Filter 1: ON PK  Fc 32 Hz   Gain  4.0 dB  Q 1.41
Filter 2: ON LS  Fc 105 Hz  Gain -3.5 dB  Q 0.9
Filter 3: ON HS  Fc 8000 Hz Gain  2.0 dB  Q 0.7
```

Supported filter types: `PK` (peak), `LS`/`LSC` (low shelf), `HS`/`HSC` (high shelf), `LP` (low pass), `HP` (high pass). Case-insensitive. Lines starting with `#` are comments.

AutoEQ presets work directly — download for your IEM and pass with `--eq`.

## HRIR Datasets

| Dataset | Measurements | Notes |
|---------|-------------|-------|
| [FABIAN (TU Berlin)](https://depositonce.tu-berlin.de/items/5504e604-3be7-4fca-b7e5-79ab680e83c0) | 11950 | Anechoic, MIT license |
| [3D3A Princeton](https://www.princeton.edu/3D3A/HRTFMeasurements.html) | 648 | Anechoic, well-documented |
| [LISTEN (IRCAM)](http://recherche.ircam.fr/equipes/salles/listen/) | 187 subjects | Free for research |
| [SADIE II](https://www.york.ac.uk/sadie-project/database.html) | Binaural + ambisonics | CC BY 4.0 |

Download a `.sofa` file and pass with `--sofa`.

## Signal Chain

```
Input channels (BlackHole 16ch, non-interleaved float32)
    │
    ├─ FL FR FC BL BR SL SR TFL TFR TBL TBR
    │   └─ NUOLS convolution × HRIR_L, HRIR_R  (vDSP FFT, 256-pt partition)
    │       └─ OLS overlap-add → stereo accumulation
    │
    └─ LFE
        └─ 4th-order Butterworth LPF @ 120 Hz → direct L+R mix
    │
    ▼
Stereo binaural (interleaved float32)
    │
    └─ Target curve EQ (biquad IIR chain, Equalizer APO format)
    │
    └─ Master gain (dB)
    │
    ▼
IEM output
```

## Architecture

```
include/hearGOD/
  types.h          — Channel enum, SpeakerPosition, AudioConfig
  fdl.h            — Frequency-Delay-Line (single OLS partition)
  nuols_engine.h   — NUOLS engine: 12-channel × FDL pair → stereo
  sofa_loader.h    — libmysofa wrapper, nearest-HRIR lookup
  biquad_chain.h   — Stereo biquad IIR chain (APO EQ)
  peq_parser.h     — Equalizer APO preset parser
  portaudio_backend.h — PortAudio real-time callback
  channel_router.h — Input index → Channel enum mapping
  config_file.h    — JSON config load/save

src/
  core/   nuols_engine.cpp  fdl.cpp
  audio/  portaudio_backend.cpp  channel_router.cpp
  io/     sofa_loader.cpp
  eq/     biquad_chain.cpp  peq_parser.cpp
  config/ config_file.cpp
  main.cpp

tests/
  test_fdl.cpp          — FDL / OLS convolution
  test_biquad.cpp       — Biquad IIR coefficients
  test_sofa.cpp         — SOFA position lookup, great-circle distance
  test_peq.cpp          — APO parser, 14 cases
  test_integration.cpp  — Full signal chain, 22 cases
```

## License

MIT
