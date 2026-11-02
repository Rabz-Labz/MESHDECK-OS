# Map pack format (`.map`)

Map packs hold pre-drawn map tiles on the Ordnance Survey National Grid, so grid references line
up exactly and the Tab5 never has to draw vectors. Made by `tools/build_map.py` from OS Open
Zoomstack; read by `core/formats/map_pack.*`; drawn by `core/apps/map_app.cpp`. Packs live in
`MESHDECK/maps/` on the SD card (any number; MAP uses every level of every pack).

## Tiles

- 256 x 256 pixels, one byte each: brightness 0 (background, e.g. sea) to 255 (full theme colour).
  MESHDECK colours them with the current theme as it draws.
- Tile (tx, ty) at a level of `mpp` metres per pixel covers eastings `[tx*256*mpp, (tx+1)*256*mpp)`
  and the same for northings; pixel row 0 is the tile's north edge.
- Each tile is PackBits-compressed (as in TIFF): a signed count byte n, then n+1 literal bytes
  (n >= 0) or one byte repeated 1-n times (n < 0); -128 is a no-op.
- A tile missing from a level's index but inside the level's area is blank (sea, or nothing
  mapped). Outside the area the level has no data, and MAP falls back to another level.

## File layout (little-endian)

| Part | Contents |
|---|---|
| Header | `"BMAP"`, u16 version (1), u16 level count (1-16) |
| Name | u8 length, then that many bytes (e.g. `uk`) |
| Attribution | u16 length, then the text (`CONTAINS OS DATA (C) CROWN COPYRIGHT...`) |
| Level table | per level, 28 bytes: u8 level number, u8 0, u16 tile size (256), f32 metres per pixel, i32 minTx, i32 minTy, i32 maxTx, i32 maxTy, u32 tile count |
| Indexes | per level in order, its tile count x 12 bytes: i16 tx, i16 ty, u32 file offset, u32 length; sorted by (ty, tx) |
| Tile data | the compressed tiles |

## The packs used on the author's deck

| Pack | Levels (m/px) | Size |
|---|---|---|
| `uk.map` | 400, 100 (all of Great Britain) | 10.5 MB |
| a local pack | 25 (with a wide margin), 6.25, 3.125 (a 20 km square) | about 144 MB |

Build them with:

```
python tools/build_map.py ZOOMSTACK.mbtiles uk.map uk
python tools/build_map.py ZOOMSTACK.mbtiles local.map local --centre E N --size-km 20
```

then copy them to the card (or `python tools/upload.py local.map local.map COM4`).

Map data: Contains OS data (c) Crown copyright and database right, Open Government Licence.
