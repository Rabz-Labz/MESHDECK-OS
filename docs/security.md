# Security

What MESHDECK's lock protects, and what it doesn't. Code: `core/services/security.*`,
`core/ui/lock_screen.cpp`, `core/apps/security_app.cpp`. Tests: `tests/test_security.cpp`.

## The PIN lock

- **PIN:** 4 to 12 digits, set in first-start setup or in SECURITY (tile 0).
- **When it locks:** at start-up, after the auto-lock time with no key or touch (OFF, 2, 5 or
  15 minutes; 5 by default), and on Ctrl+L or LOCK NOW.
- **Wrong PINs:** the first 3 in a row are free. Each further one makes you wait: 30 s, 1 min,
  5 min, 15 min, then 1 hour each time. The count is saved, so restarting doesn't skip a wait (the
  wait starts again from the restart). Nothing is ever wiped automatically.
- **While locked:** only the lock screen works. Tile keys, screenshots and the other Ctrl shortcuts
  are off. Over USB serial only `STATUS`, `POWER` and `KEY` (for typing the PIN) are accepted.

## How the PIN is stored

The PIN itself is never stored. MESHDECK keeps PBKDF2-HMAC-SHA256 of it (20,000 rounds) with a
random 16-byte salt, in a 60-byte record (layout in `security.h`). On the Tab5 the record lives
in the internal flash (NVS namespace `meshdeck`), not on the SD card, so taking the card out
doesn't remove the PIN. The simulator keeps it in a hidden file in its data folder.

## What it does not protect

- **Files on the SD card are not encrypted.** Anyone who takes the card out can read it on a PC.
  The lock stops someone using the deck, not someone with a card reader.
- **Someone with the deck and a USB cable can reflash it.** That replaces MESHDECK; it doesn't
  reveal the PIN or unlock the old firmware, but the SD card is readable anyway (above).
- 4-digit PINs are guessable over days with the waits above; use 6 or more digits if that matters.

**EMCON (radio emissions)** is on the SECURITY tile, as in 1.x. It is kept across restarts.

- **NORMAL:** messages go out and come in.
- **RX ONLY:** nothing is transmitted. Messages wait as HELD. The radio's own transmitter is switched off, so it does not relay either.
- **RADIO OFF:** MESHDECK stops using the radio, and its transmitter is switched off. The radio stays powered, because MESHDECK never powers hardware down. (1.x also cut the radio's USB power; 2.0 does not.)
