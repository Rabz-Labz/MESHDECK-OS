#!/usr/bin/env python3
"""build_gazetteer.py - the place-name index for MAP's search, from OS Open Zoomstack.

Usage:
    python tools/build_gazetteer.py ZOOMSTACK.mbtiles OUT.txt [--box E_MIN N_MIN E_MAX N_MAX]

Copy OUT.txt to MESHDECK/maps/places.txt on the SD card. Needs the same Python packages as
build_map.py (mapbox_vector_tile, Pillow).

By default the box covers the Scottish Borders, East Lothian and Northumberland
(E 300-440 km, N 545-690 km), so it also takes in the edges of Edinburgh, Midlothian and Tyneside.

Zoomstack keeps different kinds of name at different vector zooms (checked on the 2026 release):
  z10  City, Capital, Town, Suburban Area, Village, Hamlet
  z11  Landform (hills), Water (lochs, rivers)
  z12  Small Settlements
so each kind is read at its own zoom. Names are converted to OS grid metres the same way the map
packs are (build_map.py), so a found place lands exactly on the map.

Output, one place per line, sorted by name, readable on any PC:
    NAME<TAB>TYPE<TAB>EASTING<TAB>NORTHING
Map data: Contains OS data (c) Crown copyright and database right (Open Government Licence).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from build_map import Source, paths, tile_xy, to_latlon  # noqa: E402

KINDS = {
    10: {"City", "Capital", "Town", "Suburban Area", "Village", "Hamlet"},
    11: {"Landform", "Water"},
    12: {"Small Settlements"},
}
# Shorter type names for the device (shown beside each result).
SHORT = {"Capital": "CITY", "City": "CITY", "Town": "TOWN", "Suburban Area": "SUBURB", "Village": "VILLAGE",
         "Hamlet": "HAMLET", "Small Settlements": "SETTLEMENT", "Landform": "HILL", "Water": "WATER"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mbtiles")
    ap.add_argument("out")
    ap.add_argument("--box", type=float, nargs=4, default=[300000, 545000, 440000, 690000],
                    metavar=("E_MIN", "N_MIN", "E_MAX", "N_MAX"))
    a = ap.parse_args()
    e_min, n_min, e_max, n_max = a.box
    src = Source(a.mbtiles)

    # Step 1: the box in lon/lat, with a margin, to choose which vector tiles to read.
    lat_a, lon_a = to_latlon(e_min, n_min)
    lat_b, lon_b = to_latlon(e_max, n_max)
    lon0, lon1 = min(lon_a, lon_b) - 0.3, max(lon_a, lon_b) + 0.3
    lat0, lat1 = min(lat_a, lat_b) - 0.2, max(lat_a, lat_b) + 0.2

    # Step 2: read each kind at its zoom; keep names whose point falls inside the box. The same
    # name can sit in several tiles, so places are merged when within 500 m.
    found = {}
    for z, kinds in KINDS.items():
        x0, y1 = tile_xy(lon0, lat0, z)
        x1, y0 = tile_xy(lon1, lat1, z)
        tiles = 0
        for x in range(int(x0), int(x1) + 1):
            for y in range(int(y0), int(y1) + 1):
                t = src.tile(z, x, y)
                if not t:
                    continue
                tiles += 1
                layers, ae, an = t
                for f in layers.get("names", {}).get("features", []):
                    props = f["properties"]
                    kind, name = props.get("type"), props.get("name1")
                    if kind not in kinds or not name:
                        continue
                    for pt in paths(f["geometry"], "Point"):
                        e = ae[0] * pt[0] + ae[1] * pt[1] + ae[2]
                        n = an[0] * pt[0] + an[1] * pt[1] + an[2]
                        if e_min <= e <= e_max and n_min <= n <= n_max:
                            key = (name.upper(), SHORT[kind], round(e / 500), round(n / 500))
                            found[key] = (name.strip(), SHORT[kind], int(e), int(n))
        print(f"z{z}: {tiles} tiles read, {len(found)} places so far")

    # Step 3: write them sorted by name (tabs and line breaks can't appear in a name).
    rows = sorted(found.values(), key=lambda r: (r[0].upper(), r[1]))
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        for name, kind, e, n in rows:
            f.write(f"{name.replace(chr(9), ' ')}\t{kind}\t{e}\t{n}\n")
    print(f"wrote {len(rows)} places to {a.out} ({os.path.getsize(a.out):,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
