# Tab5 hardware notes

Quirks found while building MESHDECK 1.x on a real M5Stack Tab5, and how 2.0 handles each. If you
are writing your own Tab5 firmware, these may save you a day or two.

## Processor (ESP32-P4)

- **No double-precision FPU.** `double` maths is done in software and is very slow. Per-pixel
  `double` geometry made the 1.x map redraw take 3.9 s; working out values per row/column in `float`
  brought it to under 90 ms. Rule: `float` or integers in hot paths.

## microSD card

- The socket is on **SDMMC slot 0**, with the card's IO powered from the P4's **on-chip LDO
  channel 4**.
- Slot 0's pins only work through the dedicated IO_MUX. **Leave every pin unset** in
  `sdmmc_slot_config_t`; naming them makes ESP-IDF route them through the GPIO matrix and the card
  times out (error `0x107`). See `platform/tab5/src/sd_card.cpp`.
- Try 4-bit mode first, then 1-bit. Never format a card that fails to mount.
- **Cards pause for seconds during big writes.** Uploads over USB serial must allow long gaps
  (1.x uses a 30 s timeout) and be resumable.

## I2C buses

- The keyboard sits on the internal bus (`Wire`, GPIO 0/1).
- **Grove port A** (GPIO 53/54) needs the **EXT5V** output switched on before anything plugged
  in there gets power. 1.x drives port A with bit-banged I2C, because M5Unified's `Ex_I2C` would
  take over the keyboard's port.

## USB-A host and the Unit C6L (Meshtastic)

- The C6L appears as a USB CDC serial device on the Tab5's USB-A host port.
- **Don't cut the USB-A 5 V supply behind the USB host stack's back.** Doing so gave "Root port
  reset failed" and a reboot. Use the host library's own `usb_host_lib_set_root_port_power()` to
  power the port down and up.
- **A half-seated plug powers the C6L without connecting it.** USB-A power pins are longer than the
  data pins: the C6L lit up but the host saw no device at all (`RADIO` showed `host_devices=0`, and
  `RADIO LOG` was empty). Pushing the plug fully home fixed it. Before that, the link threw IN
  transfer errors and dropped.
- **A failed transfer halts its endpoint.** Resubmitting onto a halted endpoint queues transfers that
  never run, the interface can't then be released, and a device that isn't closed is never freed,
  so the port never takes a new one. `usb_radio.cpp` flushes and clears the endpoint, cancels
  transfers before closing, and power-cycles the root port if the radio doesn't come back.

## Meshtastic firmware (2.7.26 on the C6L)

- **The first region set can crash the radio** while it generates its PKI key. Set
  `security.private_key` first, then the LoRa region.
- Setting a channel URL before the region is saved resets the region to UNSET. Set the region,
  then the URL, then check both.
- Opening and closing a serial connection to the radio can reset it, so a message sent just
  before closing may be lost.
- Transmit can be disabled without powering the radio down: `AdminMessage.set_config` (field 34)
  with `Config.lora` (field 6), `tx_enabled` (LoRaConfig field 9) = false. 1.x's EMCON "RX ONLY"
  level uses this.

## Tab5 USB serial

- Closing the Tab5's own USB serial port from the PC **resets the Tab5** (DTR/RTS). PC tools keep
  the port open for the whole session.

- **With DIAGNOSTICS on screen, the USB serial link goes quiet at the 4th STATUS command** from
  the PC, every time. The device itself carries on: the screen and keys keep working. Cause not
  found.

## Keyboard

- In text mode the keyboard reports some keys by name ("enter", "delete"). The report's length
  byte includes the terminating NUL; use `strnlen` on the buffer rather than trusting it.

## Battery and power

- The INA226 on the 2S battery gives voltage and current through `M5.Power`. Readings around 4.3 V
  have been seen on USB power with no battery reading; 2.0 treats anything outside 6.0–8.8 V as
  "not measured".
- `M5.Power.getBatteryCurrent()` is **negative while discharging** and positive while charging
  (verified on battery and on USB; see [power.md](power.md)).
- `M5.Power.isCharging()` reported "charging" for a whole half-hour run **on battery**. Judge
  external power from the current's direction instead (`Tab5Platform::externalPower`).
- With the battery pack **removed** and USB connected, the voltage still reads about 8.38 V (the
  charger's output). Don't treat a voltage reading as proof a battery is fitted.
- Closing the USB serial port from the PC resets the Tab5 (see above), which also cancels any test
  running on it. Read results from the SD card, or reconnect knowing it will reset when closed.

## Display speed (measured 1 November 2026)

- **Turn kerning off for Tiny TTF fonts.** With LVGL's default (`LV_FONT_KERNING_NORMAL`) every
  label measured and drew slowly: the home screen took 451 ms to build and 530 ms to lay out. VT323
  is monospaced, so kerning does nothing; with `LV_FONT_KERNING_NONE` those became 30 ms and 18 ms.
  If a screen is mysteriously slow to appear, check the font settings first.
- **The panel is portrait (720 x 1280) in a PSRAM framebuffer.** M5GFX's `pushImage` rotates every
  pixel on the CPU when the display is used landscape: 90 ms per full screen. The ESP32-P4's 2D
  engine (PPA, `driver/ppa.h`, in the Arduino libraries) rotates each LVGL band into the framebuffer
  by DMA in 18 ms per full screen. `platform/tab5/src/display.cpp` picks the PPA's rotate/mirror
  settings by a self-test against M5GFX's own mapping, so it can't come out mirrored.
- **But the PPA rotation hangs the Tab5.** A blocking `ppa_do_scale_rotate_mirror` call never
  returned on small label redraws (TERMINAL, DIAGNOSTICS). Aligning every area to 16 pixels only
  delayed the hang. So 2.0 uses the CPU rotation path through M5GFX (`kUsePpa = false` in
  `platform/tab5/src/display.cpp`) until the fault is understood.
- **PSRAM is slow for big copies:** about 42 MB/s PSRAM to PSRAM, far faster into internal RAM. So
  LVGL draws into two 40-line band buffers in internal RAM (200 KB), and MAP writes its pixels
  straight into those bands rather than into a 1.1 MB image in PSRAM that LVGL then copies.
- Typical times now: a normal screen appears in well under 0.1 s; the map redraws in about
  0.15-0.2 s from cached tiles. Tiles read from the SD card cost about 25 ms each the first time.

## LVGL 9.2 and lodepng

- LVGL's bundled lodepng is **modified**: its PNG decode returns an `lv_draw_buf_t*` and only
  converts to ARGB. Using it as stock lodepng crashes. 2.0 uses only its zlib inflate
  (`lodepng_zlib_decompress`) behind one small wrapper. Including it from C++ needs
  `LODEPNG_NO_COMPILE_CPP`.
