# BUILD GUIDE

Assembly and calibration, in the order that works.

**Read §1 before you do anything else.** The single most common way to lose a
weekend on a build like this is to wire everything, flash it once, and then
debug five interacting faults simultaneously.

---

## 1. Build order

Each phase ends with a working, testable thing. Do not start the next phase
until the current one passes.

| Phase | Add | Test that it passes |
|---:|---|---|
| 1 | ESP32-S3 + lid servo on the bench | Servo sweeps 12°→96° smoothly, no buzz when parked |
| 2 | Both lid limit switches | `[hb]` shows the switch states changing as you move the lid by hand |
| 3 | Throat VL53L0X | `thr=` in the heartbeat tracks your hand |
| 4 | OLED | Boot screen, then the animated eye |
| 5 | WS2812B strip | Idle breathe, no flicker |
| 6 | Audio | `boot.wav` plays on reset |
| 7 | Finger cartridge (bench, unmounted) | Full hatch → pause → extend → retract cycle |
| 8 | Remote hardware | All 15 buttons print `[tx]` lines |
| 9 | ESP-NOW | `[remote] asked X -> doing Y` appears on the bin |
| 10 | Mistranslation | OPEN closes. Press it six times; compliance hits zero |
| 11 | Personality | Miss three times; the lines escalate |
| 12 | Integration | Full self-test passes, all subsystems `OK` |
| 13 | Final enclosure | Everything mounted, tray slides out, lid lifts off |

Phases 1–6 are all bench work on a breadboard. Do not mount anything in the
bin until phase 12 passes on the bench.

---

## 2. Before you start

**Tools:** soldering iron with a heat-set-insert tip, crimper for your chosen
connector, hex keys, digital calipers, a multimeter, and a bench supply with a
current limit if you have one.

**Print everything first.** `CAD_README.md` §4. The `lid_frame` is a ~9 hour
print — start it, then build the electronics while it runs.

**Install the heat-set inserts** into every printed part before assembly.
Iron at 220 °C, insert square, press until flush, let it cool before loading
it. Doing this after the part is mounted is miserable.

---

## 3. Electronics bring-up (bench)

### 3.1 Power first, and prove it

Wire the PSU → master switch → fuse → distribution bar → servo rail, with the
1000 µF bulk capacitor fitted. **Before connecting anything else:**

- [ ] Multimeter: no continuity 5 V ↔ GND
- [ ] Power on. Measure 5.0 V ±0.25 V at the servo rail
- [ ] Master switch off. Rail drops to 0 V
- [ ] GPIO38 driver: with the ESP32 unpowered, the servo rail is **off**

That last check is the important one. The safe state must be the default
state.

### 3.2 Then the logic

Power the ESP32-S3 from USB while you work — it isolates board faults from
rail faults. Only tie the two together once both are proven.

**Star ground.** Every ground returns to one bolt. Not daisy-chained, not
through the ESP32's ground pins. See `WIRING.md` §1, rule 2.

### 3.3 I²C

Bring the bus up with **one device at a time** and confirm each address:

```
0x3C  SSD1306
0x30  VL53L0X throat      (after XSHUT re-addressing)
0x31  VL53L0X approach
```

If the second ToF never appears, XSHUT is floating — that is the whole reason
GPIO47/48 exist. See `TROUBLESHOOTING.md` §3.

---

## 4. Lid assembly

1. Fit `hinge_body` ×2 to the collar; `hinge_lid` ×2 to the lid pads.
2. Slide the 4 mm pin through. **The lid must fall freely under its own
   weight** through the whole arc. If it binds anywhere, fix that now — a
   stiff hinge eats your entire torque margin and no amount of firmware will
   save it.
3. Mount `servo_mount` to the inside of the collar, screws finger-tight in
   the slots.
4. Fit the servo. **Do not fit the horn yet.**
5. Power up and let the firmware drive the servo to `LID_ANGLE_CLOSED` (12°).
6. *Now* fit the horn, at the angle that puts the lid on its closed stop.
7. Fit `linkage` between the horn and the lid anchor with M3 shoulder screws.
8. Fit `lid_stop` ×2 with their TPU pads at both ends of travel.

Step 5–6 ordering matters: fitting the horn while the servo is at an unknown
angle is how you end up with only 40° of usable travel.

### 4.1 Limit switches

Mount both microswitches so they are made **just before** the mechanical stop,
not at the same instant. The firmware wants the switch to confirm arrival
while the servo still has a degree or two in hand.

Wire **COM → GND, NC → GPIO**, so a severed wire reads as "not closed" and
faults safe.

---

## 5. Finger cartridge

Assemble and test this **on the bench, out of the bin**. It is much easier to
adjust in your hand.

1. Fit both micro servos to `servo_plate`.
2. Drive them to `DOOR_ANGLE_SHUT` (8°) and `FINGER_ANGLE_HOME` (18°), then
   fit the horns.
3. Fit `finger` to the extend servo; push the TPU `finger_tip` into its socket.
4. Fit `door` on its 2 mm pin, and link it to the door servo horn.
5. Run a full cycle. Watch for the finger fouling the hatch on the way out.

### 5.1 The reach check — do not skip this

Hold `button_plate` where it will be mounted. Run the cycle. **The finger tip
must land inside the printed witness ring**, and must overshoot the switch
face by roughly 6 mm so the switch is fully thrown.

- Tip lands short → move the button plate closer, or increase
  `FINGER_ANGLE_PRESS`.
- Tip lands long, servo strains → decrease `FINGER_ANGLE_PRESS`.
- Tip misses sideways → the cartridge is not square. Fix the mount, not the
  firmware.

Sand off the witness ring once you are happy.

---

## 6. Final assembly

Bottom-up, per the exploded diagram:

1. **Base** — TPU feet, PSU, master switch, fuse holder, IEC/barrel inlet.
2. **Body** — waste liner, `tray_rails`, then slide the tray in.
3. **Tray** — ESP32-S3, distribution perfboard, buck (if used), amp. All on
   screws. Nothing glued.
4. **Finger cartridge + button plate** — four screws, one connector.
5. **Face panel** — `oled_bezel`, eye assembly, both `tof_bracket`s, speaker
   grille.
6. **Collar** — hinge mounts, `lid_stop`s, LED strip in `led_diffuser`.
7. **Lid** — drop on, insert the hinge pin.
8. **Service panel** — last, so you can still get at everything.

Leave a **service loop** in the lid servo loom. The lid moves; the loom must
not be in tension at either end of travel.

---

## 7. Calibration

### 7.1 Lid endpoints — do this first

With the linkage fitted, in `settings.h`:

1. Set `LID_ANGLE_CLOSED` so the lid rests on its stop with the servo
   **not straining**. Listen: a servo holding against a hard stop hums. If it
   hums, back the angle off by 2° and reflash.
2. Set `LID_ANGLE_OPEN` so the lid is fully back but again not straining.
3. If the lid moves the wrong way, set `LID_INVERT = true`. Do not try to fix
   it by swapping the angles.
4. Confirm both limit switches assert at their endpoints. If
   `[lid] endpoint N not confirmed` appears, adjust the switch position, not
   the timeout.

Reference build: `LID_ANGLE_CLOSED 12`, `LID_ANGLE_OPEN 96`.

### 7.2 ToF thresholds

Watch the heartbeat and note the numbers for your geometry:

| Symbol | Set it to | Reference |
|---|---|---|
| `TOF_THROAT_OBJECT_MM` | comfortably above your empty-throat reading | 200 mm |
| `TOF_THROAT_CLEAR_MM` | 50–60 mm above `OBJECT_MM` (this gap is the hysteresis) | 260 mm |
| `SAFETY_HAND_MM` | far enough out that a hand is caught **before** the lid reaches it | 130 mm |
| `TOF_APPROACH_NEAR_MM` | where you want it to notice people | 900 mm |
| `TOF_APPROACH_FAR_MM` | 400–500 mm beyond NEAR | 1400 mm |

Test `SAFETY_HAND_MM` deliberately and repeatedly: put your hand in the
opening while the lid is closing. It must stop and reverse **every single
time**. See `TEST_PLAN.md` §2.

### 7.3 Eye endpoints

Drive the pan servo to its limits by hand-editing `EYE_PAN_MIN` / `MAX` until
the eye reaches the edges of its aperture without the yoke binding. Same for
tilt. Reference: pan 35–145, tilt 62–118.

### 7.4 Finger angles

Covered in §5.1. Reference: `DOOR_ANGLE_SHUT 8`, `DOOR_ANGLE_OPEN 92`,
`FINGER_ANGLE_HOME 18`, `FINGER_ANGLE_PRESS 112`.

### 7.5 Remote ladder

Set `CALIBRATE_LADDER 1` in the remote's `pins.h`, flash, open the serial
monitor, press each aux button, and paste the readings into `LADDER_CENTERS`.
Set it back to 0. Details in `PINOUT.md` §2.3.

### 7.6 Timing and feel

Once everything works mechanically, tune for comedy:

| Symbol | Default | Effect |
|---|---:|---|
| `LID_MS_CLOSE` | 900 | Lower = snappier, higher = more deliberate. Never below ~500. |
| `MISS_SILENCE_MS` | 2000 | The judgemental pause. **Leave it at 2000.** |
| `FINGER_PAUSE_MS` | 900 | The beat between the hatch opening and the finger appearing. This is the single most important number for the punchline. |
| `NORMAL_MODE_GRACE_MS` | 4500 | How long the bin behaves. Long enough to be convincing, short enough that the audience does not lose interest. |
| `THROW_WINDOW_MS` | 2600 | How long it waits before declaring a miss. |

---

## 8. First full power-on

- [ ] Bin on a stable surface, lid clear, hands clear
- [ ] Bench supply current-limited to 3 A if you have one
- [ ] Power on. Self-test runs: EYE, LEDS, AUDIO, LID, FINGER, SENSORS, REMOTE
- [ ] Any `FAIL` line — stop, fix, repeat. Do not carry on with a known fault
- [ ] `USELESSNESS 100%` appears
- [ ] Idle: eye drifts and blinks, LEDs breathe, lid still, servos silent
- [ ] Heartbeat every 5 s on serial, values sensible
- [ ] **Obstruction test** before letting anyone near it (`TEST_PLAN.md` §2)

Silence at idle is a real check, not a nicety — if you hear servo hunting, the
detach logic is not running, and you are wasting a few hundred mA and
shortening the servos' life.
