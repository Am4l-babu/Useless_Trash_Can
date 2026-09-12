# UselessBox

A standalone "useless box" module: a toggle switch that looks like a normal
button, but a servo arm reaches out and flips it back off. Unrelated to
BinChad/BinRemote — this runs on its own tiny board and doesn't touch the
ESP-NOW protocol or the rest of this repo's firmware.

## Hardware

| | |
|---|---|
| Board | Digispark ATtiny85 ("Digispark (Default - 16.5MHz)") |
| Servo | Signal → P0, +5V → 5V, GND → GND |
| Switch | Wired across P2 (`SWITCH_PIN`, `INPUT_PULLUP`) and GND |
| Capacitor | Across 5V/GND at the servo — needed once you push more than 3 steps per 10 ms, or the board browns out and resets |

`P1` drives the onboard LED: flashes once at boot to show it's ready, then
lights solid once the box has been flipped 4+ times within 10 s of each other
("angry" mode).

`P3` (`SWITCH_OUT`) is driven low at boot and otherwise unused by the current
logic — reserved if you want to wire a second switch state out.

## Toolchain

Follow Digistump's Arduino IDE setup first:
<http://digistump.com/wiki/digispark/tutorials/connecting>

Library: `SoftRcPulseOut` (bit-banged servo pulses sized for the ATtiny85's
limited timers) — install via Library Manager or from
<https://github.com/simonrafferty/SoftRcPulseOut>.

## Behavior

- Flip the switch → `gotoPercent()` drives the servo from `POS_START` (180°,
  resting) to a target angle back toward `POS_END` (45°, hits the switch off)
  and back, in one of ten canned sequences (`Seq00`–`Seq09`).
- Fewer than 4 rapid flips in a row (each within 10 s of the last) → always
  the plain `Seq00` response.
- 4+ rapid flips → picks a harder-to-predict sequence. At boot, whatever the
  switch reads decides the mode for that session: switch **off** at power-up
  = sequential cycling through `Seq01`–`Seq09`; switch **on** = random
  (`randomSeed()` comes from the noise floor on the floating analog pin A5).
- `gotoPercent(targetPercent, steps, afterDelay)` is the only motion
  primitive: `steps` caps how far the servo moves per 10 ms tick (keep ≤3
  without the capacitor, up to ~11 with one), and `afterDelay` idles in place
  afterward while still refreshing the pulse so the servo doesn't hunt.
