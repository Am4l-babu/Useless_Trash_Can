# TROUBLESHOOTING

The failures you are actually going to hit, roughly in the order you will hit
them.

**First, always:** open the serial monitor at 115200 and read the heartbeat.

```
[hb] IDLE/IDLE lid=0 ang=0 frus=0 thr=8190mm app=1240mm cyc=37 rx=412
```

A **frozen** heartbeat means a hung `loop()` — a software problem. A heartbeat
that keeps ticking while nothing moves means a hung mechanism — a hardware
problem. Knowing which one you have saves you an hour.

---

## 1. ESP32 resets, or `Brownout detector was triggered`

**By far the most common failure on this build.** It appears the moment the
lid first moves.

| Cause | Fix |
|---|---|
| Servos powered from the ESP32's 5 V pin | Give them their own rail from the PSU. `WIRING.md` §1, rule 1. This is almost always the answer. |
| No bulk capacitance | Fit the 1000 µF **at the servo distribution point**, not next to the PSU. |
| Bulk cap too far from the servos | Move it. Wire inductance defeats a distant capacitor. |
| Undersized supply | 5 V 5 A. A 2 A phone charger will not do this. |
| Thin power wiring | 18–20 AWG on the servo rail. Voltage drop under a 2.5 A stall is real. |
| Daisy-chained grounds | Star ground. `WIRING.md` §1, rule 2. |

Confirm with a multimeter on the servo rail while the lid moves. Below 4.6 V
is your problem.

---

## 2. Servo jitter, buzzing, or hunting

| Symptom | Cause | Fix |
|---|---|---|
| Buzzes constantly while parked | Servo still attached and hunting | The firmware detaches after `LID_IDLE_DETACH_MS`. If it does not, check `lid.update()` is being called every loop. |
| Buzzes only at an endpoint | Commanded past the mechanical stop | Back `LID_ANGLE_CLOSED`/`OPEN` off by 2–3°. **Do not leave it straining** — it will cook the servo. |
| Random twitching | Noise on the signal line | Route the signal away from servo power. Keep it short. Add 100 nF at the servo. |
| Twitches at boot | Normal — servos snap to position on attach | Cosmetic. Reduce by keeping the boot angles close to the rest positions. |
| Slow or weak | Rail sagging | See §1. |

---

## 3. A VL53L0X does not appear on the bus

| Cause | Fix |
|---|---|
| **XSHUT floating** | The usual answer. Both sensors boot at 0x29; they must be held in reset and brought up one at a time. Confirm GPIO47/48 actually reach the breakouts. |
| Wrong wiring on VIN | Adafruit breakouts take 3–5 V, some clones are 3V3-only. Check yours. |
| No pull-ups | 4.7 kΩ on SDA and SCL to 3V3. |
| Too many pull-ups | Three breakouts each with 10 kΩ gives ~3.3 kΩ, which is fine. Six is not. Cut the jumpers on the extras if the bus looks sluggish. |
| I²C run too long | Keep it under 300 mm. Twist SDA/SCL with a ground wire. |
| Address collision | The OLED is 0x3C; some are 0x3D. Change `OLED_ADDR` in `display.cpp`. |

Serial says `[sensors] THROAT ToF did not answer` — that message means
`begin()` failed, so it is a bus or XSHUT problem, not a threshold problem.

---

## 4. The sensor reads 8190 forever

8190 is the driver's out-of-range marker.

| Cause | Fix |
|---|---|
| **The bracket is shadowing the emitter** | The most common mechanical cause. `tof_bracket` holds the sensor 4 mm proud with a window wider than the PCB — do not close that window up to look neater. |
| Nothing actually in range | Expected. An empty throat genuinely reads out-of-range. |
| Target too dark or too shiny | Black matt foam and mirror-finish foil are the ToF worst cases. This is why the IR channel exists. |
| Sensor pointing into open space | The approach sensor should see a wall/floor at a known distance, not infinity. |
| Cover glass fingerprinted | Clean it. |

---

## 5. False detections / the lid opens at nothing

| Cause | Fix |
|---|---|
| Threshold too close to the resting reading | Widen the gap between `TOF_THROAT_OBJECT_MM` and the empty-throat value. |
| Not enough hysteresis | Raise `TOF_CONFIRM_SAMPLES` to 3. Keep `TOF_THROAT_CLEAR_MM` at least 50 mm above `OBJECT_MM`. |
| The lid itself entering the ToF cone | Geometry problem. Re-aim the throat sensor so it never sees the lid at any angle. Check at every point in travel, not just closed. |
| IR module triggering on ambient IR | Shade it, or turn its trim pot down. Stage lights and sunlight both emit IR. |
| Ground bounce from servo current | Star ground. §1. |

---

## 6. The lid will not move / `LID ERROR`

Display shows `LID ERROR / Have you tried turning me off?`

| Cause | Fix |
|---|---|
| A limit switch never asserted | The firmware refuses to trust the servo angle alone. Check the switch is made **just before** the mechanical stop, and that it is wired COM→GND / NC→GPIO. |
| Switch wired NO instead of NC | It will read "always closed" or "never closed". Rewire. |
| Mechanical binding | Disconnect the linkage and move the lid by hand. It must fall freely through the whole arc. Fix binding mechanically — no firmware setting compensates for a stiff hinge. |
| Servo underpowered for the lid | Redo the torque sum. `CAD_README.md` §1. |
| Servo rail off | GPIO38 low, master switch off, or a blown fuse. |
| `LID_LIMIT_TIMEOUT_MS` too short for a slow close | Only raise this once you have confirmed the mechanism is genuinely fine. |

To clear a fault without a power cycle: press the hidden trigger. The bin runs
`clearError()`, homes slowly, and displays `PRETENDING THAT'S FIXED`.

---

## 7. The remote does nothing

| Cause | Fix |
|---|---|
| **Different channels** | Both ends must call `esp_wifi_set_channel()`. The single most common ESP-NOW failure — the link silently does nothing. |
| **`protocol.h` copies have diverged** | They must be byte-identical. A mismatch fails the checksum on every packet. `diff` them. |
| USB CDC On Boot disabled on the remote | GPIO20/21 are buttons. Without USB CDC they are UART0 and the remote appears dead. |
| Different `BINCHAD_DEVICE_ID` | Both must be `0x2A`. |
| Buttons held during boot on GPIO 2/8/9 | Strapping pins. Release and reset. |
| It **is** working | Check the serial log. `[remote] asked OPEN -> doing CLOSE` means the link is perfect and the bin is simply refusing you. That is the product. |

`packetsRejected()` climbing while `packetsReceived()` stays flat means
packets are arriving and failing validation — checksum, version or device ID.

---

## 8. Audio problems

| Symptom | Cause | Fix |
|---|---|---|
| Silence | LittleFS not uploaded | Tools → ESP32 Sketch Data Upload. Serial will say `missing clip /boot.wav`. |
| Silence, and `ESP_I2S.h not found` at compile | Arduino core 2.x | Upgrade to core 3.x, or set `AUDIO_BACKEND` to `AUDIO_DFPLAYER`. |
| Silence, `I2S init failed` | Pin conflict | Check GPIO17/18/21 are not double-assigned. |
| Loud hiss between clips | Amp enabled with no data | Normal for MAX98357A. Tie SD through a resistor divider, or ignore it. |
| Distorted | Clipping | Lower `AUDIO_VOLUME_DEFAULT`, or record the source clips quieter. |
| Crackling during servo movement | Shared power rail | Separate the audio rail, add the 220 µF. |
| Plays too fast/slow | Sample rate mismatch | The parser reads the rate from the file, but only 16-bit PCM is supported. Re-export as 16-bit mono. |
| Choppy | Loop starved | Something is blocking. Look for a `delay()` you added. |

---

## 9. LED problems

| Symptom | Fix |
|---|---|
| First pixel wrong colour or flickering | 3.3 V data into a 5 V strip. Add a 74AHCT125, or run the strip from 4.5 V. |
| All pixels wrong colour | Wrong colour order. Change `NEO_GRB` to `NEO_RGB` in `leds.cpp`. |
| Flickering under load | Missing 470 µF at the strip, or missing 330 Ω on the data line. |
| Dim or brownouts at full white | 12 pixels at full white is ~720 mA. Lower `LED_BRIGHTNESS`. |
| Nothing at all | Data direction — WS2812 strips are directional. Follow the arrows. |

---

## 10. Mechanical binding

| Symptom | Fix |
|---|---|
| Lid stiff at one point in the arc | Hinge brackets not coaxial. Loosen, align, retighten. |
| Lid stiff throughout | Pin bore too tight. Raise `fit_loose` and reprint, or ream the bore. |
| Linkage binding at the extremes | `link_len` wrong. That is what the slotted holes in `servo_mount` are for. |
| Finger fouls the hatch | Cartridge not square, or `DOOR_ANGLE_OPEN` too small. |
| Finger misses the switch sideways | Fix the cartridge mount, not the firmware. `BUILD_GUIDE.md` §5.1. |
| Hatch does not close fully | Door servo horn fitted at the wrong angle. Re-fit at `DOOR_ANGLE_SHUT`. |
| Grinding noise | Something is being driven into a hard stop. **Power off immediately** and find it before you strip a gearbox. |

---

## 11. Nothing works and the demo is in ten minutes

In order:

1. **Power cycle.** Watch the self-test — it tells you which subsystem failed.
2. **Check the fuse.**
3. **Reseat every connector.** Vibration walks Dupont pins off headers.
4. Anything on the self-test says `FAIL`, disable it and demo without it:
   `USE_TOF_APPROACH 0`, `USE_IR_THROAT 0`, `USE_EYE_TILT 0`,
   `AUDIO_BACKEND AUDIO_NONE`. The machine is designed to degrade.
5. **Lid dead?** Demo the remote and the finger. Still funny.
6. **Finger dead?** Swap the spare cartridge — four screws and one connector.
7. **Sensors dead?** The hidden trigger fires a complete success reaction on
   demand. Nobody in the audience can tell.
8. **Remote dead?** The bin's own behaviour and the NORMAL MODE punchline
   carry the demo on their own.

The whole architecture is built so that no single failure takes the demo down.
Use that. Do not stand there trying to fix the root cause while the judges
wait — degrade, present, fix afterwards.
