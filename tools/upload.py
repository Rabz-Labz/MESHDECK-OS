#!/usr/bin/env python3
"""upload.py - send a map pack or the place index to the Tab5 over USB serial.

Usage:
    python tools/upload.py FILE NAME PORT
    e.g. python tools/upload.py maps-out/places.txt places.txt COM4
         python tools/upload.py uk.map uk.map /dev/ttyACM0

The file lands in MESHDECK/maps/NAME on the Tab5's SD card (NAME must be <name>.map or places.txt).
Needs pyserial. Each attempt opens the port, waits for MESHDECK to start, sends
"UPLOAD <name> <size> resume", reads "UPLOAD READY AT n" and sends from byte n in 4096-byte blocks,
waiting for 'K' after each. If it stalls (SD cards can pause for seconds), it retries and carries
on where it stopped, up to 20 times. Opening or closing the port restarts the Tab5, so a retry
waits for it to start again. Ported from MESHDECK 1.x.
"""
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    path, name, port_name = sys.argv[1:]
    data = open(path, "rb").read()
    t0 = time.time()
    for attempt in range(1, 21):
        port = serial.Serial(port_name, 115200, timeout=0.05)
        time.sleep(20 if attempt > 1 else 12)   # MESHDECK starting up after the port opened
        port.reset_input_buffer()
        port.write(f"UPLOAD {name} {len(data)} resume\n".encode())
        reply, end = b"", time.time() + 5
        while b"UPLOAD READY AT" not in reply and time.time() < end:
            reply += port.read(256)
        if b"UPLOAD READY AT" not in reply:
            print(f"attempt {attempt}: no reply ({reply[-80:]!r})", flush=True)
            port.close()
            continue
        line = reply[reply.index(b"UPLOAD READY AT"):].split(b"\n")[0]
        pos = int(line.split()[-1])
        print(f"attempt {attempt}: sending from {pos:,} of {len(data):,} bytes", flush=True)
        ok = True
        while pos < len(data):
            block = data[pos:pos + 4096]
            port.write(block)
            ack, end = b"", time.time() + 35
            while b"K" not in ack and time.time() < end:
                ack += port.read(1)
            if b"K" not in ack:
                print(f"  stalled at {pos:,} bytes", flush=True)
                ok = False
                break
            pos += len(block)
        if ok:
            result, end = b"", time.time() + 10
            while b"UPLOAD " + name.encode() not in result and time.time() < end:
                result += port.read(256)
            print(result.decode("utf-8", "replace").strip()[-120:], flush=True)
            port.close()
            print(f"done in {time.time() - t0:.0f} s", flush=True)
            return 0
        port.close()
    print("gave up after 20 attempts")
    return 1


if __name__ == "__main__":
    sys.exit(main())
