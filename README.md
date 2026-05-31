# Cardputer Stellar

Offline naked-eye planetarium for the **M5Stack Cardputer-Adv** (ESP32-S3, no PSRAM).
Renders stars (mag ≤ 6.5), constellation lines, the Sun, Moon (with phase), and the
five naked-eye planets for any UTC instant and the Cebu City horizon, with keyboard
pan/zoom, time scrub, and tap-to-inspect.

## Build


pio run -t upload        # build + flash over USB-C
pio device monitor       # 115200 baud


PlatformIO env `cardputer-adv`. First build pulls `M5Cardputer` (+ `M5Unified`,
`M5GFX`). The star catalog lives in flash (`src/data/*.h`, ~0.6 MB of `.rodata`); the
`default_8MB` partition leaves ample app space.

## Boot

Prompts for UTC as **`YYMMDDHHMM`** (10 digits), e.g. `2605291930` = 2026-05-29
19:30 UTC. Century is 20xx, seconds 0. Time then advances in real time from that
instant. No battery-backed RTC, so the time is re-entered each boot.

## Controls


,  pan left      ;  pan up        =  zoom in       [  time -30 min
/  pan right     .  pan down      -  zoom out       ]  time +30 min
enter  select nearest star to the reticle (recenters + info panel)
backspace  dismiss panel        space  pause / resume time


Panning/zoom use tap-and-glide: each keypress nudges a camera target and the view
eases toward it. Selecting a star slides an info panel in from the left (label,
magnitude, spectral type, constellation, RA/Dec, live alt/az) and parks the star
beside it. The lower-right HUD shows look altitude, azimuth, and 16-point heading.

## Accuracy

Positions use a low-precision Keplerian ephemeris (Schlyter) — arcminute-class, more
than enough for naked-eye work. Validated: Sun seasonal declinations (±23.44°), Moon
illumination at known new/full moons, and Venus/Mars/Jupiter positions + magnitudes.
Star positions are J2000 (no precession applied — intended for near-term scrubbing).
Saturn's ring brightening is ignored (mag may be ~0.5 off). Moon and Sun discs are
drawn at a fixed minimum radius so the phase is visible (not to angular scale).

## Regenerating the catalog


python3 tools/gen_catalog.py [mag_limit]    # default 6.5


Pulls the HYG database and Stellarium western constellation lines, emits
`src/data/star_catalog.h`, `star_names.h`, `constellations.h`. A lower magnitude
limit yields fewer stars and less flash use.

## On-device verification

The math is validated on host; these depend on the board and may need a small tweak:

1. **Keyboard symbol keys** — digit/`, ; . / - = [ ]` reads come from the keyboard
   `word` buffer; `enter`/`del`/`space` from its flags. If a key reads oddly, the fix
   is localized to `handle_char` / `boot_prompt` in `main.cpp`.
2. **HUD blend** — the translucent HUD uses `readPixel`/`drawPixel` over a small rect
   (`blend_dark_rect`); confirm `M5Canvas` readback behaves on the 16bpp sprite.
3. **GFX API names** — `begin(cfg, true)`, `keysState()` fields, text datums, and
   `&fonts::Font0` follow M5Cardputer/M5GFX conventions.

Memory: one 240×135×16bpp canvas (~64 KB SRAM); catalog stays in flash. No large
heap allocations.
