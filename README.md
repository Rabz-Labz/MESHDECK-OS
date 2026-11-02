# MESHDECK CR // OS

An offline field operating system for the **M5Stack Tab5** (ESP32-P4, 5" 1280×720 touchscreen,
keyboard), built for emergencies: off-grid mesh messaging over Meshtastic, offline Ordnance Survey
maps, a reference library with PDF and EPUB books, a field logbook, a terminal, a few games and a
PIN lock. Everything works with no internet and no phone network.

> **Status: 2.0 in development (milestones V0 to V7 built).** MESHDECK 1.x is the working
> version on the author's device; 2.0 is a clean rebuild of it. See [docs/roadmap.md](docs/roadmap.md).

The user manual is in [docs/manual/MESHDECK-manual.pdf](docs/manual/MESHDECK-manual.pdf) (source:
[docs/manual/MESHDECK-manual.html](docs/manual/MESHDECK-manual.html)).

MESHDECK ships with no map data. To make Ordnance Survey maps and a place-search list for your own
area and put them on the Tab5, follow [docs/maps.md](docs/maps.md).

## What's here

| Folder | What it is |
|---|---|
| `core/` | Platform-independent C++: the kernel (events, settings, storage, journal, clock), services and apps. No Arduino or ESP-IDF code. |
| `platform/tab5/` | The Tab5 firmware: hardware drivers and the glue that runs `core` on the device. |
| `platform/sim/` | A desktop build of the same code for development and screenshots. |
| `tests/` | Unit tests for `core`, run by CTest and by CI on every change. |
| `tools/` | PC tools: map and book builders, the import tool, the uploader. |
| `docs/` | Architecture, file formats, hardware notes, power measurements, the user manual. |
| `third_party/` | Bundled libraries with their licences (see [THIRD_PARTY.md](THIRD_PARTY.md)). |

## Building

You need CMake 3.20+ and a C++17 GCC or Clang (on Windows, MinGW-w64; MSVC isn't supported yet).

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The Tab5 firmware builds with [PlatformIO](https://platformio.org):

```
pio run -d platform/tab5
```

## Hardware

- M5Stack Tab5 with its keyboard, and a microSD card (FAT32) holding your maps ([docs/maps.md](docs/maps.md)).
- Optional: M5Stack Unit C6L (Meshtastic radio) on the USB-A port.

Quirks found on real hardware are written up in [docs/hardware-notes.md](docs/hardware-notes.md).

## Licence

MIT; see [LICENSE](LICENSE). Bundled libraries and data keep their own licences
([THIRD_PARTY.md](THIRD_PARTY.md)). Map data: contains OS data © Crown copyright and database
right, used under the Open Government Licence.
