"""Build a MESHDECK map pack from OS Open Zoomstack vector tiles (MBTiles).

The pack holds 256 x 256 pixel, 8-bit brightness tiles laid out on the OS National Grid, so the
Tab5 draws them in the current theme colour and grid references line up exactly. Tiles are
PackBits-compressed and indexed in one file. Map data: Contains OS data (c) Crown copyright and
database right, released under the Open Government Licence.

  python build_map.py ZOOMSTACK.mbtiles OUT.map uk
  python build_map.py ZOOMSTACK.mbtiles OUT.map local --centre E N --size-km 20

"uk" renders the whole country at 400 and 100 m per pixel. "local" renders a square around a
centre (OS grid metres) at 25 m/px (with a wide margin), 6.25 and 3.125 m/px.
"""
import argparse, math, os, sqlite3, struct, sys, time, zlib
import mapbox_vector_tile
from PIL import Image, ImageDraw, ImageFont

TILE = 256
SS = 2                      # supersampling for smooth lines and text
FONT = os.path.join(os.path.dirname(__file__), "..", "core", "ui", "fonts", "VT323-Regular.ttf")
ATTRIBUTION = "CONTAINS OS DATA (C) CROWN COPYRIGHT AND DATABASE RIGHT 2026"

# ----------------------------------------------------------------------------- OS grid (as osgrid.cpp)
DEG = math.pi / 180
AIRY_A, AIRY_B = 6377563.396, 6356256.909
WGS_A, WGS_B = 6378137.0, 6356752.314245
F0, LAT0, LON0, E0, N0 = 0.9996012717, 49 * DEG, -2 * DEG, 400000, -100000
TO_OSGB = (-446.448, 125.157, -542.060, 20.4894, -0.1502, -0.2470, -0.8421)
TO_WGS = tuple(-v for v in TO_OSGB)


def cart(lat, lon, a, b):
    e2 = 1 - b * b / (a * a)
    nu = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    return nu * math.cos(lat) * math.cos(lon), nu * math.cos(lat) * math.sin(lon), (1 - e2) * nu * math.sin(lat)


def uncart(x, y, z, a, b):
    e2 = 1 - b * b / (a * a)
    p = math.hypot(x, y)
    lat = math.atan2(z, p * (1 - e2))
    for _ in range(10):
        nu = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
        lat = math.atan2(z + e2 * nu * math.sin(lat), p)
    return lat, math.atan2(y, x)


def helmert(h, x, y, z):
    tx, ty, tz, s, rx, ry, rz = h
    s *= 1e-6
    sec = DEG / 3600
    rx, ry, rz = rx * sec, ry * sec, rz * sec
    return (tx + (1 + s) * x - rz * y + ry * z, ty + rz * x + (1 + s) * y - rx * z, tz - ry * x + rx * y + (1 + s) * z)


def meridional(lat):
    n = (AIRY_A - AIRY_B) / (AIRY_A + AIRY_B)
    dl, sl = lat - LAT0, lat + LAT0
    return AIRY_B * F0 * ((1 + n + 1.25 * n * n + 1.25 * n ** 3) * dl - (3 * n + 3 * n * n + 2.625 * n ** 3) * math.sin(dl) * math.cos(sl)
                          + (1.875 * n * n + 1.875 * n ** 3) * math.sin(2 * dl) * math.cos(2 * sl) - 35 / 24 * n ** 3 * math.sin(3 * dl) * math.cos(3 * sl))


def to_grid(lat_deg, lon_deg):
    la, lo = uncart(*helmert(TO_OSGB, *cart(lat_deg * DEG, lon_deg * DEG, WGS_A, WGS_B)), AIRY_A, AIRY_B)
    a, b = AIRY_A, AIRY_B
    e2 = 1 - b * b / (a * a)
    s, c, t = math.sin(la), math.cos(la), math.tan(la)
    nu = a * F0 / math.sqrt(1 - e2 * s * s)
    rho = a * F0 * (1 - e2) / (1 - e2 * s * s) ** 1.5
    eta2 = nu / rho - 1
    I = meridional(la) + N0
    II = nu / 2 * s * c
    III = nu / 24 * s * c ** 3 * (5 - t * t + 9 * eta2)
    IIIA = nu / 720 * s * c ** 5 * (61 - 58 * t * t + t ** 4)
    IV = nu * c
    V = nu / 6 * c ** 3 * (nu / rho - t * t)
    VI = nu / 120 * c ** 5 * (5 - 18 * t * t + t ** 4 + 14 * eta2 - 58 * t * t * eta2)
    dl = lo - LON0
    return E0 + IV * dl + V * dl ** 3 + VI * dl ** 5, I + II * dl ** 2 + III * dl ** 4 + IIIA * dl ** 6


def to_latlon(e, n):
    a, b = AIRY_A, AIRY_B
    e2 = 1 - b * b / (a * a)
    phi, m = LAT0, 0
    while True:
        phi = (n - N0 - m) / (a * F0) + phi
        m = meridional(phi)
        if abs(n - N0 - m) < 0.00001:
            break
    s, c, t = math.sin(phi), math.cos(phi), math.tan(phi)
    nu = a * F0 / math.sqrt(1 - e2 * s * s)
    rho = a * F0 * (1 - e2) / (1 - e2 * s * s) ** 1.5
    eta2 = nu / rho - 1
    VII = t / (2 * rho * nu)
    VIII = t / (24 * rho * nu ** 3) * (5 + 3 * t * t + eta2 - 9 * t * t * eta2)
    IX = t / (720 * rho * nu ** 5) * (61 + 90 * t * t + 45 * t ** 4)
    X = 1 / c / nu
    XI = 1 / c / (6 * nu ** 3) * (nu / rho + 2 * t * t)
    XII = 1 / c / (120 * nu ** 5) * (5 + 28 * t * t + 24 * t ** 4)
    XIIA = 1 / c / (5040 * nu ** 7) * (61 + 662 * t * t + 1320 * t ** 4 + 720 * t ** 6)
    de = e - E0
    la = phi - VII * de ** 2 + VIII * de ** 4 - IX * de ** 6
    lo = LON0 + X * de - XI * de ** 3 + XII * de ** 5 - XIIA * de ** 7
    la, lo = uncart(*helmert(TO_WGS, *cart(la, lo, AIRY_A, AIRY_B)), WGS_A, WGS_B)
    return la / DEG, lo / DEG


# ----------------------------------------------------------------------------- vector tiles
def lonlat_of(z, x, y, px, py, extent):
    """Web Mercator tile-local coordinates (y down) to lon/lat."""
    n = 2 ** z
    lon = (x + px / extent) / n * 360 - 180
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + py / extent) / n))))
    return lon, lat


def tile_xy(lon, lat, z):
    n = 2 ** z
    x = (lon + 180) / 360 * n
    y = (1 - math.asinh(math.tan(lat * DEG)) / math.pi) / 2 * n
    return x, y


def fit_affine(points):
    """Least squares (x, y) -> v for v = a*x + b*y + c."""
    sxx = sxy = syy = sx = sy = 0.0
    n = len(points)
    for x, y, _ in points:
        sxx += x * x; sxy += x * y; syy += y * y; sx += x; sy += y
    m = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
    r = [sum(x * v for x, y, v in points), sum(y * v for x, y, v in points), sum(v for _, _, v in points)]
    # Solve 3x3 by Cramer's rule.
    def det(a):
        return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    d = det(m)
    out = []
    for col in range(3):
        mc = [row[:] for row in m]
        for i in range(3):
            mc[i][col] = r[i]
        out.append(det(mc) / d)
    return out


class Source:
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        self.cache = {}

    def tile(self, z, x, y):
        key = (z, x, y)
        if key in self.cache:
            return self.cache[key]
        row = self.db.execute("select tile_data from tiles where zoom_level=? and tile_column=? and tile_row=?",
                              (z, x, (2 ** z - 1) - y)).fetchone()
        result = None
        if row:
            data = row[0]
            if data[:2] == b"\x1f\x8b":
                data = zlib.decompress(data, 16 + zlib.MAX_WBITS)
            layers = mapbox_vector_tile.decode(data, default_options={"y_coord_down": True})
            extent = next(iter(layers.values()))["extent"] if layers else 4096
            # Affine from tile-local coordinates to OS grid metres, fitted on a 3 x 3 grid of points.
            pts_e, pts_n = [], []
            for fx in (0, 0.5, 1):
                for fy in (0, 0.5, 1):
                    lon, lat = lonlat_of(z, x, y, fx * extent, fy * extent, extent)
                    e, n = to_grid(lat, lon)
                    pts_e.append((fx * extent, fy * extent, e))
                    pts_n.append((fx * extent, fy * extent, n))
            result = (layers, fit_affine(pts_e), fit_affine(pts_n))
        if len(self.cache) > 600:
            self.cache.clear()
        self.cache[key] = result
        return result


# ----------------------------------------------------------------------------- styling
# Brightness values: 0 is the background (sea), 255 is full theme ink.
LAND, FORESHORE, URBAN, WOOD, GREEN, BUILDING = 26, 14, 52, 40, 36, 92
ROADS = {  # type: (brightness, width in final pixels, minimum level)
    "Motorway": (255, 3.0, 0), "Primary": (230, 2.4, 0), "A Road": (215, 2.0, 0), "B Road": (190, 1.6, 1),
    "Minor": (160, 1.3, 2), "Local": (130, 1.0, 3), "Restricted": (110, 0.8, 3), "Guided Busway": (100, 0.8, 3),
}
NAME_TYPES = {  # level -> (types in priority order, font px)
    0: (["Country", "City"], 22),
    1: (["City", "Town"], 20),
    2: (["City", "Town", "Suburban Area", "Village"], 20),
    3: (["Town", "Suburban Area", "Village", "Hamlet", "Small Settlements"], 20),
    4: (["Town", "Suburban Area", "Village", "Hamlet", "Small Settlements", "Water", "Woodland", "Greenspace"], 18),
}


def mvt_zoom(m_per_px):
    """Coarsest vector zoom whose detail is fine enough for this resolution (at 54 N)."""
    for z in range(0, 15):
        if 156543.03 * math.cos(54 * DEG) / 2 ** z / 2 <= m_per_px:
            return z
    return 14


def paths(geom, kind):
    t, c = geom["type"], geom["coordinates"]
    if t == kind:
        return [c]
    if t == "Multi" + kind:
        return c
    return []


def draw_tile(src, level, mpp, tx, ty, labels):
    tile_m = TILE * mpp
    e0, n0 = tx * tile_m, ty * tile_m
    n1 = n0 + tile_m
    size = TILE * SS
    scale = size / tile_m
    img = Image.new("L", (size, size), LAND)
    draw = ImageDraw.Draw(img)
    z = mvt_zoom(mpp)
    # Vector tiles covering this grid square (plus a little margin).
    corners = [to_latlon(e, n) for e, n in ((e0, n0), (e0 + tile_m, n0), (e0, n1), (e0 + tile_m, n1))]
    xs, ys = [], []
    for lat, lon in corners:
        x, y = tile_xy(lon, lat, z)
        xs.append(x); ys.append(y)
    tiles = []
    for x in range(int(min(xs)), int(max(xs)) + 1):
        for y in range(int(min(ys)), int(max(ys)) + 1):
            t = src.tile(z, x, y)
            if t:
                tiles.append(t)
    if not tiles:
        return None

    def proj(ae, an, pts):
        return [((ae[0] * px + ae[1] * py + ae[2] - e0) * scale, (n1 - (an[0] * px + an[1] * py + an[2])) * scale) for px, py in pts]

    def fill_layer(name, value, outline=None, width=1):
        mask = Image.new("L", (size, size), 0)
        md = ImageDraw.Draw(mask)
        any_drawn = False
        for layers, ae, an in tiles:
            for f in layers.get(name, {}).get("features", []):
                for poly in paths(f["geometry"], "Polygon"):
                    for i, ring in enumerate(poly):
                        p = proj(ae, an, ring)
                        if len(p) >= 3:
                            md.polygon(p, fill=255 if i == 0 else 0)
                            any_drawn = True
                            if outline is not None:
                                draw.line(p + [p[0]], fill=outline, width=width)
        if any_drawn:
            img.paste(value, mask=mask)

    def line_layer(name, value, width, types=None):
        for layers, ae, an in tiles:
            for f in layers.get(name, {}).get("features", []):
                if types is not None and f["properties"].get("type") not in types:
                    continue
                for line in paths(f["geometry"], "LineString"):
                    p = proj(ae, an, line)
                    if len(p) >= 2:
                        draw.line(p, fill=value, width=max(1, int(round(width * SS))), joint="curve")

    fill_layer("sea", 0)
    fill_layer("foreshore", FORESHORE)
    fill_layer("urban_areas", URBAN)
    if level >= 1:
        fill_layer("woodland", WOOD)
    if level >= 2:
        fill_layer("greenspaces", GREEN)
    fill_layer("surfacewater", 0)
    if level >= 2:
        line_layer("contours", 48 if level >= 3 else 40, 0.6, None if level >= 3 else {"Index"})
    if level >= 1:
        line_layer("waterlines", 0, 1.4 if level >= 3 else 1.0, None if level >= 2 else {"National", "MHW"})
    if level >= 3:
        fill_layer("buildings", BUILDING)
    if level >= 1:
        line_layer("rail", 150, 1.0)
    for rtype, (value, width, minlevel) in sorted(ROADS.items(), key=lambda kv: kv[1][0]):
        if level >= minlevel:
            line_layer("roads", 0, width + 1.2, {rtype}) if level >= 3 else None   # dark casing on big scales
            line_layer("roads", value, width, {rtype})

    # Labels placed for the whole level in advance (see place_labels), drawn where they touch.
    font = ImageFont.truetype(FONT, NAME_TYPES[level][1] * SS)
    for text, e, n in labels:
        x, y = (e - e0) * scale, (n1 - n) * scale
        w = font.getlength(text)
        if x + w / 2 < -10 or x - w / 2 > size + 10 or y < -60 or y > size + 60:
            continue
        draw.text((x, y), text, font=font, fill=255, anchor="mm", stroke_width=3 * SS // 2, stroke_fill=0)
    # Outside the OS data area (0-700 km east, 0-1250 km north) there is nothing mapped: blank it,
    # or the default land shade would show as a strip beyond the coast.
    if e0 + tile_m > 700000:
        draw.rectangle([(700000 - e0) * scale, 0, size, size], fill=0)
    if n1 > 1250000:
        draw.rectangle([0, 0, size, (n1 - 1250000) * scale], fill=0)
    small = img.resize((TILE, TILE), Image.LANCZOS)
    return small.tobytes()


def place_labels(src, level, mpp, e_min, n_min, e_max, n_max):
    """Greedy, collision-free label placement over the whole level, so tiles agree at their edges."""
    types, px = NAME_TYPES[level]
    z = mvt_zoom(mpp)
    lat_a, lon_a = to_latlon(e_min, n_min)
    lat_b, lon_b = to_latlon(e_max, n_max)
    x0, y1 = tile_xy(min(lon_a, lon_b) - 0.5, min(lat_a, lat_b) - 0.3, z)
    x1, y0 = tile_xy(max(lon_a, lon_b) + 0.5, max(lat_a, lat_b) + 0.3, z)
    found = {}
    rows = src.db.execute("select tile_column, tile_row from tiles where zoom_level=? and tile_column between ? and ?",
                          (z, int(x0), int(x1))).fetchall()
    for col, trow in rows:
        y = (2 ** z - 1) - trow
        if y < int(y0) or y > int(y1):
            continue
        t = src.tile(z, col, y)
        if not t:
            continue
        layers, ae, an = t
        for f in layers.get("names", {}).get("features", []):
            props = f["properties"]
            if props.get("type") not in types or not props.get("name1"):
                continue
            for pt in paths(f["geometry"], "Point"):
                e = ae[0] * pt[0] + ae[1] * pt[1] + ae[2]
                n = an[0] * pt[0] + an[1] * pt[1] + an[2]
                if e_min <= e <= e_max and n_min <= n <= n_max:
                    key = (props["name1"].upper(), round(e / (mpp * 40)), round(n / (mpp * 40)))
                    found[key] = (types.index(props["type"]), props["name1"].upper(), e, n)
    font = ImageFont.truetype(FONT, px)
    placed, boxes = [], []
    for prio, text, e, n in sorted(found.values()):
        w, h = font.getlength(text) + 8, px + 6
        x, y = e / mpp, -n / mpp
        box = (x - w / 2, y - h / 2, x + w / 2, y + h / 2)
        if any(not (box[2] < b[0] or box[0] > b[2] or box[3] < b[1] or box[1] > b[3]) for b in boxes):
            continue
        boxes.append(box)
        placed.append((text, e, n))
    return placed


# ----------------------------------------------------------------------------- pack file
def packbits(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes([257 - run, data[i]])
            i += run
            continue
        start = i
        while i < n and i - start < 128:
            if i + 2 < n and data[i] == data[i + 1] == data[i + 2]:
                break
            i += 1
        out += bytes([i - start - 1]) + data[start:i]
    return bytes(out)


def write_pack(path, levels, name):
    """levels: list of (level, mpp, (minTx, minTy, maxTx, maxTy), {(tx, ty): bytes})."""
    head = bytearray(b"BMAP" + struct.pack("<HH", 1, len(levels)))
    nm = name.encode()[:31]
    at = ATTRIBUTION.encode()
    head += struct.pack("<B", len(nm)) + nm + struct.pack("<H", len(at)) + at
    level_hdr_size = 28
    index_start = len(head) + level_hdr_size * len(levels)
    index_sizes = [12 * len(t) for _, _, _, t in levels]
    data_start = index_start + sum(index_sizes)
    blobs, offset = [], data_start
    level_hdrs, indexes = bytearray(), bytearray()
    idx_off = index_start
    for (lvl, mpp, (a, b, c, d), tiles), isz in zip(levels, index_sizes):
        level_hdrs += struct.pack("<BBHfiiiiI", lvl, 0, TILE, mpp, a, b, c, d, len(tiles))
        for (tx, ty) in sorted(tiles, key=lambda k: (k[1], k[0])):
            comp = packbits(tiles[(tx, ty)])
            indexes += struct.pack("<hhII", tx, ty, offset, len(comp))
            blobs.append(comp)
            offset += len(comp)
        idx_off += isz
    # Each level header is followed (implicitly) by its index entries in level order.
    with open(path + ".tmp", "wb") as f:
        f.write(head)
        f.write(level_hdrs)
        f.write(indexes)
        for blob in blobs:
            f.write(blob)
    os.replace(path + ".tmp", path)
    return offset


def render_level(src, level, mpp, e_min, n_min, e_max, n_max):
    tile_m = TILE * mpp
    tx0, ty0 = int(e_min // tile_m), int(n_min // tile_m)
    tx1, ty1 = int((e_max - 1) // tile_m), int((n_max - 1) // tile_m)
    labels = place_labels(src, level, mpp, tx0 * tile_m, ty0 * tile_m, (tx1 + 1) * tile_m, (ty1 + 1) * tile_m)
    tiles, total, t0 = {}, (tx1 - tx0 + 1) * (ty1 - ty0 + 1), time.time()
    done = 0
    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            data = draw_tile(src, level, mpp, tx, ty, labels)
            done += 1
            if data and any(data):
                tiles[(tx, ty)] = data
        print(f"  level {level} ({mpp} m/px): row {ty - ty0 + 1}/{ty1 - ty0 + 1}, {len(tiles)} tiles kept, "
              f"{time.time() - t0:.0f}s", flush=True)
    return (level, mpp, (tx0, ty0, tx1, ty1), tiles)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mbtiles")
    ap.add_argument("out")
    ap.add_argument("kind", choices=["uk", "local", "region"])
    ap.add_argument("--centre", nargs=2, type=float)
    ap.add_argument("--size-km", type=float, default=20)
    ap.add_argument("--bbox", nargs=4, type=float, metavar=("E0", "N0", "E1", "N1"),
                    help="region: OS grid metres; or a file holding the four numbers")
    ap.add_argument("--area-file", help="region: file with E0 N0 E1 N1 on its first line (kept private)")
    args = ap.parse_args()
    src = Source(args.mbtiles)
    levels = []
    if args.kind == "uk":
        for level, mpp in ((0, 400.0), (1, 100.0)):
            levels.append(render_level(src, level, mpp, 0, 0, 700000, 1250000))
        name = "UK OVERVIEW"
    elif args.kind == "region":
        # A larger area (e.g. a county or two): 25 m/px with a 20 km margin, 6.25 m/px over the area.
        box = args.bbox
        if args.area_file:
            with open(args.area_file, encoding="utf-8") as f:
                box = [float(v) for v in f.readline().split()[:4]]
        if not box:
            sys.exit("region needs --bbox E0 N0 E1 N1 or --area-file")
        e0, n0, e1, n1 = box
        levels.append(render_level(src, 2, 25.0, e0 - 20000, n0 - 20000, e1 + 20000, n1 + 20000))
        levels.append(render_level(src, 3, 6.25, e0, n0, e1, n1))
        name = "LOCAL DETAIL"
    else:
        if not args.centre:
            sys.exit("local needs --centre E N (OS grid metres)")
        e, n = args.centre
        half = args.size_km * 500
        margin = max(half, 15000)
        levels.append(render_level(src, 2, 25.0, e - half - margin, n - half - margin, e + half + margin, n + half + margin))
        for level, mpp in ((3, 6.25), (4, 3.125)):
            levels.append(render_level(src, level, mpp, e - half, n - half, e + half, n + half))
        name = "LOCAL DETAIL"
    size = write_pack(args.out, levels, name)
    print(f"wrote {args.out}: {sum(len(l[3]) for l in levels)} tiles, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
