# Third-party software and data

MESHDECK itself is MIT-licensed. It includes or uses the following, each under its own licence.

| Component | Use | Licence |
|---|---|---|
| [LVGL](https://lvgl.io) 9.2.2 | User interface (`third_party/lvgl`, source unmodified; licence in `third_party/lvgl/LICENCE.txt`) | MIT |
| [SDL2](https://libsdl.org) | Simulator window (not included; installed separately) | zlib |
| [M5Unified / M5GFX](https://github.com/m5stack/M5Unified) | Tab5 display, touch, power, RTC drivers | MIT |
| [doctest](https://github.com/doctest/doctest) 2.4.11 | Unit tests (`tests/doctest.h`) | MIT |
| [VT323](https://fonts.google.com/specimen/VT323) font by Peter Hull | The interface typeface (`core/ui/fonts/`, licence in `OFL.txt` there) | SIL Open Font License 1.1 |
| [OS Open Zoomstack](https://www.ordnancesurvey.co.uk/products/os-open-zoomstack) | Map packs built by `tools/` (not stored in this repository) | Open Government Licence v3.0; on-screen attribution "Contains OS data © Crown copyright and database right" |
| Meshtastic protocol (field numbers) | Mesh messaging over the Meshtastic serial API | Protocol definitions GPL-3.0; MESHDECK has its own minimal encoder written from the published field numbers, and contains no Meshtastic code |

Books converted for the Library (e.g. OpenStax under CC BY 4.0, Project Gutenberg public-domain
texts) are **not** part of this repository; `tools/` converts files you supply.
