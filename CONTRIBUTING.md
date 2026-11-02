# Contributing to MESHDECK

Thank you for helping. MESHDECK is meant for people relying on it in an emergency, so clarity and
honesty matter more than cleverness.

## The rules

1. **No invented data.** If a value isn't known (no clock, no fix, no sensor), the screen says so.
2. **Explain the code.** Every file starts with a comment saying what it is for. Every function
   says what it does, what it takes and returns, and its steps in order. Anything non-obvious gets
   a short "why" note.
3. **Keep `core/` portable.** No Arduino, ESP-IDF or SDL headers in `core/`; hardware access goes
   through `core/platform/platform.h`.
4. **Hot paths use `float` or integers.** The ESP32-P4 has no double-precision FPU.
5. **Tests with changes.** New behaviour in `core/` comes with a test in `tests/`; all tests must
   pass (`ctest`).
6. **No secrets or personal data** in commits: signing keys, channel keys, PINs, home locations,
   journals or converted books. `.gitignore` covers the usual places.

## Style

`clang-format` with the repository's `.clang-format` (4 spaces, 120 columns). Names:
`CamelCase` types, `camelCase` functions and variables, `member_` with a trailing underscore,
`kConstant` constants.

## Pull requests

Describe what changed and how you tested it (simulator, unit tests, which hardware). Screenshots
from the simulator help for anything on screen.
