# Book pack format (`.bk`)

A book pack holds one book for the LIBRARY reader. Packs live in `MESHDECK/books/` on the SD card.
Two kinds exist:

- **Pages** (kind 1): a PDF, one 16-grey image per page, plus each page's text for searching.
- **Text** (kind 2): an EPUB, cut into sections of text. The Tab5 can make these itself from an EPUB
  in `MESHDECK/Files/Inbox` (FILES > select the file > IMPORT AS BOOK).

Everything is little-endian. Strings are a `u16` byte length followed by that many bytes of UTF-8.
The reader holds only the header, contents and index in memory; pages and text are read from the
card when needed.

## Layout

| Offset | Field | Notes |
|---|---|---|
| 0 | `"BBOK"` | magic, 4 bytes |
| 4 | `u16` version | must be `2` |
| 6 | `u8` kind | `1` = Pages, `2` = Text |
| 7 | reserved | one byte, `0` |
| 8 | title, author, category, source | four strings |
| after | `u32` count | number of pages or sections, at most 200,000 |
| after | `u32` tocCount | number of contents entries, at most 20,000 |
| after | contents | `tocCount` entries, each: `u32` index, `u8` level, string title |
| after | index | `count` entries, each: `u32` imgOff, `u32` imgLen, `u32` txtOff, `u32` txtLen |
| later | blobs | page images and section texts, in any order |

- Every offset is an absolute position from the start of the file.
- Strings are limited to 1,000 bytes. A contents entry whose index is not below `count` is dropped.
- A section with `txtLen` 0 is empty and is shown as blank.

## Page image blob (Pages only)

- `u16` width, `u16` height. Width must be even, at most 4,096; height at most 8,192.
- Then a zlib stream (`imgLen - 4` bytes) of 4-bit grey levels, two per byte, high nibble first.
- Each level `n` (0 to 15) is shown as `n * 17` (0 = background, 255 = ink), so ink is bright.
- The decompressed size must be exactly `width * height / 2` bytes.

## Section text blob (Text only)

- Plain UTF-8 text with `\n` between paragraphs. No markup.
- Sections made by the Tab5 importer are at most about 6,000 characters, cut at paragraph
  breaks where possible.

## Contents

- A contents entry points at a page or section by its index (0-based).
- Pack builders should give each chapter or heading one entry. The Tab5 importer uses the
  first line of each section, cut to about 40 characters, as the title.

## Errors the reader reports

| Message | Meaning |
|---|---|
| `CAN'T OPEN` | the file can't be read |
| `NOT A BOOK PACK` | no `BBOK` magic |
| `UNSUPPORTED BOOK VERSION (CONVERT AGAIN)` | version or kind not known |
| `DAMAGED HEADER`, `DAMAGED CONTENTS`, `DAMAGED INDEX` | the file is cut short or corrupt |
| `NO PAGE IMAGE`, `CAN'T READ THE PAGE` | a page is missing or unreadable |
| `PAGE IMAGE DAMAGED (SIZE)`, `PAGE IMAGE DAMAGED (n)` | the image blob is wrong; `n` is the zlib error |

## Writing a pack

1. Write the header and contents, then the index with placeholder offsets, then the blobs.
2. Go back and fill in the real offsets in the index, then write the file to a temporary name and
   rename it, so a power cut never leaves half a book.
