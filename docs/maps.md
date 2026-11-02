# Maps for your own area

MESHDECK ships with no map data. MAP draws **map packs** (`*.map`) and searches a **place list**
(`places.txt`). You make both on a PC from free Ordnance Survey data, then put them in
`MESHDECK/maps/` on the Tab5's SD card. This guide takes you from nothing to a working map of your
own area. It uses Exeter, in Devon, as the example. Use your own area instead.

**Coverage: Great Britain only.** The tools use OS data and the OS National Grid, and neither
covers Northern Ireland, the Isle of Man or the Channel Islands. There's no tool for those yet.

## What you need

- A PC (Windows, macOS or Linux) with about 10 GB of free disk space.
- **Python 3.10 or newer** (3.12 is tested) from [python.org](https://www.python.org/downloads/).
  On Windows, tick "Add Python to PATH" when installing.
- A copy of this repository (the `tools/` folder and `core/ui/fonts/VT323-Regular.ttf`, which the
  map builder uses for labels).
- A microSD card formatted **FAT32**. Windows only offers FAT32 for cards up to 32 GB. For a bigger
  card, use the SD Association's "SD Memory Card Formatter" or a FAT32 formatting tool.

## 1. Download the source data

1. Go to the OS Data Hub's open data downloads: <https://osdatahub.os.uk/downloads/open>.
2. Choose **OS Open Zoomstack**, format **Vector Tiles** (an `.mbtiles` file), and download it.
   It's free and needs no account. The file covers all of Great Britain and is a few gigabytes.
3. Keep the file somewhere easy to find, for example `C:\maps\OS_Open_Zoomstack.mbtiles`. The
   commands below call it `ZOOMSTACK.mbtiles`.

The data is released under the **Open Government Licence v3.0**. Packs made by `build_map.py`
carry the required attribution, and MAP shows it on screen: *Contains OS data © Crown copyright
and database right.* Keep that wording if you share packs you've made.

## 2. Install the PC tools

From the repository's top folder, install the three Python packages the tools use:

```
python -m pip install mapbox-vector-tile Pillow pyserial
```

(On macOS and Linux the command may be `python3`.) You need `pyserial` only for the USB upload in
step 5b.

## 3. Find your area on the OS grid

The tools take positions as OS National Grid **eastings and northings in metres** (for example
E 292300, N 92900 for central Exeter). To convert a latitude and longitude, which any map app or
website will show you, use the repository's own converter:

```
python -c "import sys; sys.path.insert(0, 'tools'); from build_map import to_grid; print([round(v) for v in to_grid(50.7256, -3.5269)])"
```

That prints `[292323, 92888]`. If you have a grid reference such as **SX 923 928**, the letters
give the 100 km square and the digits give the position inside it. You can also type the grid
reference into POSITION on the Tab5 later to check it.

For an area rather than a point, you need a **box**: the smallest easting and northing (south-west
corner), then the largest (north-east corner). Convert the two corners as above. Example: a 50 km
box around Exeter is `267000 68000 317000 118000`.

## 4. Build the packs and the place list

Run these from the repository's top folder. Each one prints its progress and ends with the file's
size. Big areas take a long time to render, so start with a small one.

**a. The overview of Great Britain** (about 10 MB, 400 and 100 m per pixel). Everyone should have
this one: it shows the whole country when you zoom out.

```
python tools/build_map.py ZOOMSTACK.mbtiles uk.map uk
```

**b. Detail for your area.** Pick one:

- *A town and its surroundings:* a square of `--size-km` around a centre, at 25, 6.25 and
  3.125 m per pixel. A 20 km square is about 150 MB.

  ```
  python tools/build_map.py ZOOMSTACK.mbtiles exeter.map local --centre 292300 92900 --size-km 20
  ```

- *A larger area, such as a county:* a box at 25 m per pixel (with a 20 km margin) and 6.25 m per
  pixel. It's less detailed than `local` but covers more ground per megabyte.

  ```
  python tools/build_map.py ZOOMSTACK.mbtiles devon.map region --bbox 267000 68000 317000 118000
  ```

  To keep your area out of your shell history, put the four numbers on the first line of a text
  file and pass `--area-file area.txt` instead of `--bbox`. `area.txt` is git-ignored.

You can have as many packs as you like. MAP uses every level of every pack, and picks the most
detailed one for each place on screen.

**c. The place list for SEARCH.** It covers towns, villages, hamlets, small settlements, hills and
water inside a box. **Always pass `--box`**: without it, the tool falls back to a built-in default
box that probably isn't your area.

```
python tools/build_gazetteer.py ZOOMSTACK.mbtiles places.txt --box 267000 68000 317000 118000
```

The file must be called `places.txt`, and there is one per card, so make its box cover every area
you have packs for. It's a plain text file, one place per line: name, type, easting, northing,
separated by tabs.

## 5. Put them on the card

**a. Copy them to the SD card (recommended).** Switch the Tab5 off and put its card in your PC.
Copy the files into the `MESHDECK/maps/` folder. MESHDECK creates this folder on first start; if the
card is new, create it yourself.

```
MESHDECK/
  maps/
    uk.map
    exeter.map
    places.txt
```

Pack names can be anything ending in `.map` (lower case). Put the card back and start the Tab5.

**b. Or send them over USB.** Connect the Tab5 to the PC by USB-C and find its serial port
(Windows: Device Manager > Ports, e.g. `COM4`; Linux: `/dev/ttyACM0`; macOS: `/dev/cu.usbmodem…`).
Close anything else using the port, then send one file at a time:

```
python tools/upload.py uk.map uk.map COM4
python tools/upload.py exeter.map exeter.map COM4
python tools/upload.py places.txt places.txt COM4
```

The name on the device must be `<name>.map` (under 40 characters) or `places.txt`, and an SD card
must be fitted. Opening the port restarts the Tab5, so the tool waits for MESHDECK to start before
sending. Large packs take a while over serial. If the transfer stalls, the tool resumes where it
stopped, up to 20 times. Copying to the card directly (5a) is much faster.

## 6. Check it on the Tab5

1. Press **2** (MAP). The map should draw in the theme colour with grid lines, with the OS
   attribution at the bottom.
   - "NO MAP PACK: COPY .MAP FILES TO MESHDECK/MAPS ON THE SD CARD" means no `.map` file was found.
     Check the folder name and the `.map` ending.
   - "MAP <file>: <error>" means a file is there but isn't a valid pack (for example, a copy that
     didn't finish). Copy it again.
   - "NO MAP DATA HERE AT THIS ZOOM" means the view is outside your packs. Zoom out, or search for
     a place inside your area.
2. Press **S** (search) and type at least two letters of a town in your area. The list shows how
   many places are in the index. Choose one: the map centres on it and marks it.
   - "NO PLACE INDEX: COPY PLACES.TXT TO MESHDECK/MAPS" means `places.txt` is missing or misnamed.
3. Zoom in with **+** until you reach the detailed levels of your local or region pack. Lines and
   labels should stay sharp.
4. Optional: set where you are in POSITION (a grid reference such as `SX 923 928`, or a latitude
   and longitude), then press **C** to centre the map on you.

If you add or replace a pack while MESHDECK is running, restart the Tab5 so MAP finds it.

## Updating

OS refreshes Zoomstack a few times a year. To update, download the new file and run steps 4 and 5
again. Packs and the place list from different releases work together.

See [formats/map-pack.md](formats/map-pack.md) for the `.map` file layout.
