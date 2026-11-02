# Roadmap

MESHDECK 2.0 is built in milestones. Each has an exit test that must pass before the next starts.
Until V7, MESHDECK 1.x stays on the author's Tab5 and keeps working.

| Milestone | Deliverable | Exit test | Status |
|---|---|---|---|
| **V0 Foundation** | Repo layout, licence, kernel (events, settings, storage, journal, clock), platform interface, unit tests, portable firmware build, CI, power baseline | CI builds and tests on a clean machine; baseline power table recorded | Built; power table and first CI run to do |
| **V1 Shell and UX** | 11-tile home (keys 1-9, 0 and A), big controls, screen templates, global keys, screenshots (Ctrl+P), status bar with runtime, first-start setup, lock screen | Every app usable by touch alone and by keyboard alone | Done 1 Nov 2026 (passed on the Tab5) |
| **V2 Power** | Brightness in SETTINGS > DISPLAY only; no sleep, power-down or presets (owner's decision) | Brightness works on the Tab5 | Built; to check on the Tab5 |
| **V3 Messages** | Chat with any node or channel, contacts with nicknames, delete thread, unread counts | Two-radio tests pass; deleted thread gone after restart | Done 1 Nov 2026 (passed on the Tab5 with the Cardputer: channel and direct messages, acks, nickname, delete) |
| **V4 Map** | Big zoom buttons, place search (Scottish Borders, East Lothian, Northumberland), contacts on markers | Search for a town centres the map; redraw under 100 ms | Built and running on the Tab5 (25,058 places; search, pan, zoom, position, nodes). Redraw ~0.15-0.2 s from cached tiles with the PPA display path (was ~0.8 s); first view of an area adds SD reads. The Tab5 now uses the CPU rotation path, because the PPA path hangs the device (see `platform/tab5/src/display.cpp`); redraw time to measure again |
| **V5 Library and Files** | PDF/EPUB reader with zoom and bookmarks, on-device EPUB import, PC import tool, image viewer | A new EPUB in the Inbox is readable without a PC | Done 2 November 2026: reader, FILES viewer and on-device EPUB import from the Inbox passed on the Tab5 |
| **V6 Logbook** | Hub with Log entries, Reports, Inventory, Notes | All 1.x data opens unchanged | Built: all four parts are real screens. Passed on the Tab5: LOG ENTRIES, the hub and REPORTS (save only; sending reports over the mesh was removed on purpose). To check on the Tab5: NOTES save, INVENTORY items |
| **V7 Settings and the rest** | Settings with Clock and Power; Terminal, Diagnostics, Security, Games (RALLY pong, SEA BATTLE, KLONDIKE), Tools | 1.x feature parity | Built: TERMINAL; GAMES (RALLY, SEA BATTLE, KLONDIKE; checked on the Tab5); TOOLS on key A (timer, stopwatch, calculator); DIAGNOSTICS pages (TOUCH TEST, KEY LOG, RADIO); EMCON radio levels in SECURITY (NORMAL / RX ONLY / RADIO OFF; RADIO OFF switches the transmitter off but does not cut the radio's power); boot splash. Dropped by the owner: emergency info on the lock screen, emergency wipe, the NFC card as a second key, NFC tag linking for inventory, Lua apps, the ENV Pro sensors |
| **V8 Release** | User manual, screenshots, signed release builds, migration notes | A new user gets through setup from the README alone | |
| **Field acceptance** | Cold start offline, missing hardware, low battery, day-long and solar runs | Measured and recorded | |
