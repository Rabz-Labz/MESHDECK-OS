# Journal file format (`.jnl`)

Journals are MESHDECK's append-only logs: the system log, the field log and messages. The format is designed so that a power cut at any moment loses at most the record that
was being written, and never damages earlier records.

Code: `core/kernel/journal.h` / `.cpp`. Tests: `tests/test_journal.cpp`.

## Layout

A journal is a sequence of records with no file header. Each record is a 28-byte header followed by
`length` bytes of UTF-8 text. All numbers are little-endian.

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 2 | magic | `0x4A42` ("BJ") |
| 2 | 1 | type | see below |
| 3 | 1 | flags | 0 (reserved) |
| 4 | 2 | length | text length, at most 4000 |
| 6 | 2 | reserved | 0 |
| 8 | 4 | sequence | 1, 2, 3... in file order |
| 12 | 8 | utc | Unix seconds (UTC); 0 when the clock was not set |
| 20 | 4 | uptime | milliseconds since start-up |
| 24 | 4 | crc | CRC-32 (IEEE, as zip/PNG) of bytes 0–23 followed by the text |

### Record types (system journal)

| Value | Name | Text |
|---:|---|---|
| 1 | Boot | e.g. `BOOT MESHDECK 2.0 V0` |
| 2 | Settings | a changed setting |
| 3 | ClockSet | old and new time |
| 4 | Test | used by the test suite |
| 5 | Note | free text |

Other journals (messages) define their own types in their own format notes.

## Reading and recovery

On open, the file is read from the start. Each record must have the right magic, a length of at
most 4000, the whole text present, and a matching CRC. The first record that fails any check marks
the end of good data: everything from there on is counted as "discarded" and new records overwrite
it. Sequence numbers continue from the last good record.

## Writing

Each append writes the header and text in one call, then flushes and syncs the file before
returning, so a record reported as written is on the card.

## Compatibility

The layout is unchanged from MESHDECK 1.x, so 1.x journals open in 2.0 as they are.
