# Power measurements

Battery life matters most in an emergency, so every power-saving feature in MESHDECK 2.0 is
measured against a baseline before it is kept. Figures are measured on the Tab5's own battery
sensor (INA226), **on battery with no USB power connected**.

## Method

1. Charge the internal battery, put the SD card in, and connect the radio if it is part of the run.
2. Send `POWERTEST` over USB serial. You then have 60 s to unplug USB.
3. The firmware steps through each state for 5 minutes (ignoring the first 30 s while things
   settle), samples battery voltage and current every second, and appends one row per state to
   `MESHDECK/powertest.csv` on the SD card: state, average/min/max mA, average mV, samples, whether
   external power was seen, uptime. Six states take 30 minutes; afterwards brightness and radio go
   back to normal.
4. Copy the CSV from the card. Discard any row with `external_power=1`.
5. Repeat once to check the figures agree within about 5 %.

For the baseline this test is built into MESHDECK 1.x (`firmware/src/main.cpp`, class `PowerTest`),
so it measures the current software as it is.

The live figures are also available at any time from the serial `POWER` command and (from V1) on
the Diagnostics power page.

**Sign convention:** MESHDECK 2.0's core counts positive mA as current flowing out of the battery
(discharging). The CSV holds M5Unified's raw reading, which is the opposite: **negative while
discharging** (about −216 mA on battery) and positive while charging (+600 to +750 mA on USB),
verified 1 November 2026. `Tab5Platform::batteryMilliamps` flips the sign.

**No battery fitted:** on USB power with the battery pack removed, the INA226 still reads about
8.38 V (the charger's output), which 1.x shows as 98 %. A voltage reading alone doesn't prove a
battery is present.

## Baseline (MESHDECK 1.x behaviour)

Run 2, 1 November 2026: internal battery, C6L radio connected, idle home screen, 270 samples per
state (4.5 min) unless noted. Battery about 8.1 V throughout.

| State (CSV name) | Average mA | Power | Notes |
|---|---:|---:|---|
| `screen100_radio_on` | *to measure* | | not measured yet; see below |
| `screen50_radio_on` | 212 (208–260) | 1.73 W | run 1 gave 217 |
| `screen20_radio_on` | 204 (202–252) | 1.66 W | run 1 gave 216 |
| `backlight_off_radio_on` | 162 (157–169) | 1.31 W | |
| `screen50_radio_off` | 167 (166–171) | 1.36 W | |
| `screen50_radio_on_cpu_busy` | 227 (223–278) | 1.83 W | 253 samples |

What each part costs (differences between rows):

| Part | Share | Saving if switched off |
|---|---:|---|
| Backlight at 50 % | ≈ 50 mA | ≈ 24 % of the total |
| Backlight at 20 % | ≈ 42 mA | dimming 50 → 20 % saves only ≈ 8 mA |
| C6L radio (listening) | ≈ 45 mA | ≈ 21 % |
| CPU busy vs "idle" | ≈ 15 mA | small, because 1.x never really idles |
| Everything else (P4 at full speed, PSRAM, display panel, Wi-Fi chip) | ≈ 117 mA | the biggest share |

## What the numbers showed (V0 analysis)

- **The base load is the biggest target.** With the backlight off and the radio off, about
  117 mA (≈ 0.95 W) remains. Busy work adds only 15 mA, which means the "idle" CPU is already
  running flat out: 1.x's main loop polls every 2 ms and never sleeps. Frequency scaling plus
  light sleep between events, and holding the unused Wi-Fi co-processor off, are V2's main work.
- **Screen off saves about a quarter;** dimming saves little. A short screen timeout (backlight
  off, any key or touch wakes it) is worth much more than lower brightness. Profiles should turn
  the backlight off rather than just dim it.
- **The radio costs about a fifth.** It has to listen to receive, so "radio sleeps with the screen"
  stays optional, for RESERVE only.
- **Runtime today** (2000 mAh assumed until the pack size is confirmed): about 9.5 h at 50 %
  screen with the radio on, about 12 h with the backlight off.
- **Decision D3:** the evidence points at CPU power management. First try enabling ESP-IDF power
  management inside the Arduino build (pioarduino can rebuild the framework with a custom
  sdkconfig); move to plain ESP-IDF only if that can't reach light sleep.

## MESHDECK 2: brightness only (V2, decided 1 November 2026)

The owner's decision: **no sleep mode, no powering down and no
presets.** The screen always stays on and nothing is switched off behind the user's back. The one
power control is the screen brightness, set in SETTINGS > DISPLAY (10-100 %, + and -).

## Test notes

- The 100 % state is not measured yet. It needs one more short run, and its "time in state" must be
  checked for a wrap-around to a huge number before it is used.
- `M5.Power.isCharging()` isn't reliable on the Tab5, so MESHDECK 2.0 reads external power from the
  current's sign instead (negative = discharging in M5Unified's raw reading).

## What the numbers decide

- How much a screen timeout saves, and so the default timeouts for each power profile.
- Whether the radio's share is worth a "radio sleeps with the screen" option.
- Decision D3 (ESP-IDF vs Arduino): if the CPU idle share is large, automatic light sleep (which
  needs ESP-IDF's power management) is worth the move.
