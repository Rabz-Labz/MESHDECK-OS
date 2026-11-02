# Architecture

MESHDECK is split so that almost all of it is ordinary, portable C++ that can be built and tested on
a PC. Only a thin layer knows about the Tab5.

```
core/          portable C++17: no Arduino, no ESP-IDF, no LVGL in the kernel
  platform/    platform.h: the one interface the core needs from the hardware
  kernel/      clock, storage (paths and safe writes), journal, settings, event bus
  services/    (V1+) mesh, position, power, security, contacts, books, maps, high scores, terminal
  ui/          (V1+) theme, widgets, screen templates, shell
  apps/        (V1+) one folder per app
  formats/     (V1+) map pack, book pack, PNG/BMP, OS grid, protobuf, zlib wrapper
platform/
  tab5/        the firmware: drivers and main.cpp (PlatformIO)
  sim/         (V1) desktop build with an SDL window and fake hardware
tests/         doctest unit tests for core, run by CTest and CI
tools/         PC tools (map and book builders, import, upload)
docs/          this folder
```

## Rules

1. **The core never includes hardware headers.** Anything it needs from the device (time, the
   RTC, where the SD card is mounted, battery readings, logging) goes through `meshdeck::Platform`
   in `core/platform/platform.h`. The Tab5 and the simulator each implement it once. No `#ifdef`
   mazes.
2. **Apps never reach into each other.** Services publish events on the `EventBus` (message
   received, battery level, lock state...) and apps subscribe. Events are queued and delivered from
   the main loop, so a handler never runs in the middle of another one.
3. **All file locations come from `Paths`** (`core/kernel/storage.h`). Nothing else spells out
   `MESHDECK/...`.
4. **Files are written safely.** Whole files (settings, contacts, positions) are written with
   `writeFileAtomic`: write a temporary file, sync, then rename over the old one. Growing logs are
   `Journal`s: append-only records with a CRC each, so a power cut loses at most the last record
   (format: [formats/journal.md](formats/journal.md)).
5. **No double-precision maths in hot paths.** The ESP32-P4 has a single-precision FPU only; use
   `float` or integers, and work out per-row/per-column values once rather than per pixel.
6. **Be honest about what isn't known.** A clock that was never set reads "time not set", not
   2000-01-01; a battery reading outside the possible range is reported as missing.

## The user interface (V1)

- **Shell** (`core/ui/shell.h`): owns the screen. Status bar at the top, hint line at the bottom,
  and between them either the home screen (11 tiles and a NOW panel) or the open app inside the
  standard frame (BACK, title, the app's action buttons, then its body).
- **Apps** (`core/ui/app.h`, `core/apps/`): fill the body in `build(Frame&)`, keep their state in
  members (the shell may rebuild the screen at any time, e.g. on a theme change), and get keys the
  shell doesn't use through `onKey`.
- **Widgets** (`core/ui/widgets.h`): buttons, tiles, panels, lists and the hub template, all
  themed and at least 64 px tall. Every pressable widget joins the keyboard focus list.
- **Keys**: every keyboard (the Tab5's, the simulator's PC keyboard, the serial `KEY` command)
  calls `Shell::key(code, mods)`. The shell takes global keys first (Ctrl shortcuts, ESC, tile
  digits, arrows), offers the rest to the app, and passes anything left to LVGL through its own
  keypad device. Arrow keys move focus to the nearest control in that direction.
- **Threading**: LVGL callbacks never replace the screen. Navigation is queued with
  `Shell::later()` and carried out by `Shell::tick()` from the main loop.
- **Screenshots**: `lv_snapshot` renders the screen into PSRAM and `PngWriter` streams it to
  `MESHDECK/Files/Screenshots/` one row at a time.

The simulator (`platform/sim`) runs exactly this code in an SDL window, and
`meshdeck-sim --shots <dir>` saves every screen as a PNG. The unit tests drive the same shell
headless (`tests/test_shell.cpp`).

## Start-up

1. M5Unified brings up the display, touch, power chip and RTC; the keyboard driver starts.
2. The SD card is mounted (`platform/tab5/src/sd_card.cpp`) and `Paths::createAll()` makes the
   folders.
3. Settings load and a `Boot` record is appended to `MESHDECK/log/system.jnl`.
4. LVGL gets the display (two 80-line buffers in PSRAM) and touch; the shell builds the home screen.
5. The main loop feeds keys to the shell, runs LVGL, the shell and the event bus, and answers
   serial commands.

## Building

| What | Command |
|---|---|
| Core, simulator + tests (any PC) | `cmake -S . -B build -G Ninja` then `cmake --build build` and `ctest --test-dir build` |
| Run the simulator | `build/platform/sim/meshdeck-sim` (needs SDL2; skipped if CMake can't find it) |
| Tab5 firmware | `pio run -d platform/tab5` |

The firmware uses `core/` and `third_party/lvgl` in place (`symlink://` entries in
`platform/tab5/platformio.ini`); `core/library.json` tells PlatformIO which files to compile.
LVGL's settings are in `config/lv_conf.h`, shared by every build.
