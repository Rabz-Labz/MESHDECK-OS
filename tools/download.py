#!/usr/bin/env python3
"""download.py - fetch files from the Tab5's SD card over USB serial.

Usage:
    python tools/download.py PORT --list [FOLDER]
    python tools/download.py PORT PATH [OUT]
    python tools/download.py PORT --latest-screenshot [OUT]
    e.g. python tools/download.py COM4 --latest-screenshot
         python tools/download.py COM4 log/journal.txt journal.txt

PATH and FOLDER are inside MESHDECK/ on the card (e.g. Files/Screenshots/2026-11-02_1735.png).
OUT defaults to the file's own name in the current folder. Needs pyserial, and the Tab5 unlocked.

The port is opened with DTR and RTS off, which leaves MESHDECK running; if it doesn't answer, it
may be restarting, so this waits and asks again. "DOWNLOAD <path>" makes the Tab5 send
"DOWNLOAD SIZE n" and then the bytes in 4096-byte blocks, waiting for 'K' after each.
"""
import os
import re
import sys
import time

import serial

SCREENSHOTS = "Files/Screenshots"


def open_port(name: str) -> serial.Serial:
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = name, 115200, 0.05
    port.dtr = port.rts = False   # toggling these restarts the Tab5
    port.open()
    time.sleep(0.5)
    port.reset_input_buffer()
    return port


def read_until(port: serial.Serial, marker: bytes, seconds: float) -> bytes:
    data, end = b"", time.time() + seconds
    while marker not in data and time.time() < end:
        data += port.read(4096)
    return data


def command(port: serial.Serial, line: str, marker: bytes) -> bytes:
    """Sends a command and reads up to marker, once more after 20 s if MESHDECK was restarting."""
    for wait in (5, 20):
        port.reset_input_buffer()
        port.write((line + "\n").encode())
        reply = read_until(port, marker, wait)
        if marker in reply or b"REJECTED" in reply or b"LOCKED" in reply:
            return reply
    return reply


def list_folder(port: serial.Serial, folder: str) -> list:
    reply = command(port, f"FILES {folder}".rstrip(), b"FILES END")
    if b"FILES END" not in reply:
        raise SystemExit(reply.decode("utf-8", "replace").strip()[-160:] or "no reply from the Tab5")
    entries = []
    for line in reply.decode("utf-8", "replace").splitlines():
        if line.startswith("DIR "):
            entries.append(("dir", 0, line[4:]))
        elif line.startswith("FILE "):
            size, name = line[5:].split(" ", 1)
            entries.append(("file", int(size), name))
    return entries


def screenshot_order(name: str):
    """Dated names (2026-11-02_1735.png, then _2, _3 in the same minute) after numbered ones."""
    m = re.fullmatch(r"(\d{4}-\d{2}-\d{2}_\d{4})(?:_(\d+))?\.png", name)
    if m:
        return (1, m.group(1), int(m.group(2) or 1))
    m = re.fullmatch(r"screenshot_(\d+)\.png", name)
    return (0, "", int(m.group(1))) if m else (-1, name, 0)


def download(port: serial.Serial, path: str, out: str) -> int:
    reply = command(port, f"DOWNLOAD {path}", b"DOWNLOAD SIZE")
    m = re.search(rb"DOWNLOAD SIZE (\d+)\r?\n", reply)
    if not m:
        print(reply.decode("utf-8", "replace").strip()[-160:] or "no reply from the Tab5")
        return 1
    size, data = int(m.group(1)), reply[m.end():]
    while len(data) < size:
        want = min(len(data) // 4096 * 4096 + 4096, size)   # end of the block in flight
        end = time.time() + 35
        while len(data) < want and time.time() < end:
            data += port.read(want - len(data))
        if len(data) < want:
            print(f"stalled at {len(data):,} of {size:,} bytes")
            return 1
        port.write(b"K")
    data = data[:size]
    if b"DOWNLOAD OK" not in read_until(port, b"DOWNLOAD OK", 5):
        print("the Tab5 didn't confirm the download")
        return 1
    with open(out, "wb") as f:
        f.write(data)
    print(f"saved {out} ({size:,} bytes)")
    return 0


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    port = open_port(args[0])
    try:
        if args[1] == "--list":
            for kind, size, name in list_folder(port, args[2] if len(args) > 2 else ""):
                print(f"{name}/" if kind == "dir" else f"{size:>10,}  {name}")
            return 0
        if args[1] == "--latest-screenshot":
            shots = [name for kind, _, name in list_folder(port, SCREENSHOTS) if kind == "file"]
            if not shots:
                print("no screenshots on the card")
                return 1
            name = max(shots, key=screenshot_order)
            return download(port, f"{SCREENSHOTS}/{name}", args[2] if len(args) > 2 else name)
        path = args[1]
        return download(port, path, args[2] if len(args) > 2 else os.path.basename(path))
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
