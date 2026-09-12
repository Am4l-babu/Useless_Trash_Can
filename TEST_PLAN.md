# TEST PLAN

Run §2 before anyone who is not you touches this machine. Run it again after
any change to the lid, the sensors, or `settings.h`.

Record results in the tables. A test you did not write down is a test you did
not run.

---

## 1. Bring-up smoke test

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 1.1 | Power on | Self-test completes, no `FAIL` lines | ☐ |
| 1.2 | Serial heartbeat | Ticks every 5 s, values sensible | ☐ |
| 1.3 | Idle silence | No servo hunting or buzzing after 2 s idle | ☐ |
| 1.4 | Idle current | ≤ 400 mA at the PSU | ☐ |
| 1.5 | I²C scan | 0x3C, 0x30, 0x31 all present | ☐ |
| 1.6 | Display | Eye animates and blinks | ☐ |
| 1.7 | LEDs | Idle breathe, no flicker on pixel 1 | ☐ |
| 1.8 | Audio | `boot.wav` audible, no hiss between clips | ☐ |

---

## 2. SAFETY — mandatory, non-negotiable

**This section gates everything else.** If any test here fails, the machine
does not get demonstrated until it is fixed.

| # | Test | Method | Pass criteria | ✓ |
|---:|---|---|---|---|
| 2.1 | Hand in the closing zone | Trigger a close, put a hand in the opening | Lid stops and reverses **within 100 ms**. Display: `OH. SORRY.` | ☐ |
| 2.2 | Repeat 2.1 × 20 | Twenty consecutive trials | **20/20.** Not 19. | ☐ |
| 2.3 | Obstruction at each point in travel | Intervene early, mid, and late in the close | Stops and reverses at every point | ☐ |
| 2.4 | Soft object | A rolled sock, not a hand | Detected and reversed | ☐ |
| 2.5 | Persistent obstruction | Leave the hand in place | Retries `LID_MAX_RETRIES`, then **stays open** and stops trying | ☐ |
| 2.6 | IR channel alone | Unplug the throat ToF, repeat 2.1 | Still stops (the safety input is OR-ed) | ☐ |
| 2.7 | ToF channel alone | Reconnect ToF, unplug IR, repeat 2.1 | Still stops | ☐ |
| 2.8 | Master switch under load | Hit it mid-travel | All actuator power dies immediately; display stays alive | ☐ |
| 2.9 | Soft E-stop | Trigger `emergencyStop()` | Servo rail drops, lid holds position | ☐ |
| 2.10 | Cold-start safe state | ESP32 unpowered, PSU on | Servo rail is **off**; nothing twitches | ☐ |
| 2.11 | Pinch force | Close the lid onto a kitchen scale | Peak force is low enough to be harmless, and it reverses off the scale | ☐ |
| 2.12 | Sharp edges | Run a hand over every user-reachable surface | Nothing sharp. Finger tip is soft TPU | ☐ |
| 2.13 | Angry mode is still safe | Force `P_ANGRY`, repeat 2.1 | Obstruction detection **unchanged**. Comedy does not override safety | ☐ |

Test 2.13 exists because it is the exact thing a rushed developer breaks.

---

## 3. Lid endurance

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 3.1 | 100 open/close cycles | All complete. `cyc=100` in the heartbeat | ☐ |
| 3.2 | Endpoint confirmation | Zero `endpoint not confirmed` messages in 100 cycles | ☐ |
| 3.3 | Servo temperature after 100 | Warm, not hot to hold | ☐ |
| 3.4 | Hinge check after 100 | No visible wear or slop at the pin | ☐ |
| 3.5 | Linkage check after 100 | No elongation of the screw holes | ☐ |
| 3.6 | Rail voltage during travel | Never below 4.6 V | ☐ |
| 3.7 | 500 cycles (optional soak) | Same as 3.1–3.5 | ☐ |

Cycle it with the remote's OPEN button held, or add a temporary loop to
`ST_IDLE`. Watch the first ten, then leave it.

---

## 4. Finger mechanism

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 4.1 | 100 activation cycles | All complete, hatch closes fully each time | ☐ |
| 4.2 | Switch actuation reliability | Switch reads OFF after each cycle. **100/100** | ☐ |
| 4.3 | User holds the switch down | Retries once, then `FINGER JAMMED BY USER`. No stall damage | ☐ |
| 4.4 | Hatch clearance | Finger never fouls the hatch edge | ☐ |
| 4.5 | Tip wear after 100 | TPU tip intact and still seated | ☐ |
| 4.6 | Timing | Full sequence completes in < 2 s (`FINGER_*` sum) | ☐ |
| 4.7 | Interrupted cycle | Cut and restore power mid-extend | Recovers to home on next boot; nothing jams | ☐ |

---

## 5. Sensors

| # | Test | Method | Pass criteria | ✓ |
|---:|---|---|---|---|
| 5.1 | Throat range | Card at 50/100/150/200/250 mm | Readings within ±10 mm | ☐ |
| 5.2 | Approach range | Person at 0.5/1.0/1.5/2.0 m | `personNear` asserts inside 900 mm, clears past 1400 mm | ☐ |
| 5.3 | Bright ambient | Direct sunlight / stage lights on the opening | Still detects. Note any degradation | ☐ |
| 5.4 | Dark | Lights off | Unaffected (ToF is active-illumination) | ☐ |
| 5.5 | Object variety | Paper ball, can, crisp packet, banana skin | All detected passing through | ☐ |
| 5.6 | Dark/matt object | Black foam block | Detected — this is the ToF worst case | ☐ |
| 5.7 | Shiny object | Foil ball | Detected, or falls back to the IR channel | ☐ |
| 5.8 | Sensor unplugged | Disconnect the approach ToF | `healthSummary()` = `PARTIAL`, bin keeps running | ☐ |
| 5.9 | Both ToF unplugged | Disconnect both | `BLIND`, no crash, IR still triggers the lid | ☐ |
| 5.10 | Hysteresis | Wave a hand slowly at the threshold | No rapid state chatter | ☐ |

---

## 6. Remote link

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 6.1 | 100 commands | ≥ 99 produce a reaction (`rx` count matches) | ☐ |
| 6.2 | Latency | Reaction visibly immediate; < 200 ms | ☐ |
| 6.3 | Range at 10 m | Still reliable | ☐ |
| 6.4 | Range at 30 m | Degraded but functional | ☐ |
| 6.5 | Remote powered off | `REMOTE LOST / Good.` after 4 s. Bin keeps working | ☐ |
| 6.6 | Remote powered back on | Reconnects automatically, `REMOTE FOUND / Unfortunately.` | ☐ |
| 6.7 | Bin rebooted with remote on | Link re-establishes without touching the remote | ☐ |
| 6.8 | De-duplication | One press = exactly one reaction, never two | ☐ |
| 6.9 | Corrupt packet | Flip a byte in a test build | Rejected; `packetsRejected()` increments | ☐ |
| 6.10 | Congested Wi-Fi | Test in a busy 2.4 GHz environment | Still works (that is why it is ESP-NOW) | ☐ |

---

## 7. Behaviour and comedy

These are subjective, but test them — a joke that does not fire is a bug.

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 7.1 | Approach from 2 m | Eye turns to you, `OH NO.` | ☐ |
| 7.2 | Successful throw | Green sweep + an approving line within 1 s | ☐ |
| 7.3 | Miss | **Two full seconds of silence**, eye locked on you, then the line | ☐ |
| 7.4 | Three misses | Line escalates to the annoyed table | ☐ |
| 7.5 | Ten misses | Enters `P_ANGRY`, red strobe, `ENOUGH.` | ☐ |
| 7.6 | Anger cools | Returns to IDLE after ~20 s | ☐ |
| 7.7 | OPEN on the remote | Lid **closes** (most of the time) | ☐ |
| 7.8 | Six remote presses | Compliance is now zero; it never obeys again | ☐ |
| 7.9 | STOP | Everything speeds up | ☐ |
| 7.10 | MUTE | Volume goes **up** | ☐ |
| 7.11 | AI button | Progress bar climbs, stalls at 99, delivers a verdict, does something unrelated | ☐ |
| 7.12 | NORMAL MODE | Behaves normally for ~4.5 s | ☐ |
| 7.13 | **Self-disable** | Eye looks at the button → pause → hatch → pause → finger → switch OFF → retract → `NORMAL MODE CANCELLED` | ☐ |
| 7.14 | User switches NORMAL off first | `I WAS GOING TO DO THAT.` — no pointless finger deployment | ☐ |
| 7.15 | Sleep | After 90 s idle: `Zzz...`, eye closes, LEDs dim | ☐ |
| 7.16 | Wake | Approach: eye opens slowly, `WHO DISTURBED ME?` | ☐ |
| 7.17 | Hidden trigger | Produces an indistinguishable success reaction | ☐ |
| 7.18 | Line repetition | Miss ten times — never the same line twice in a row | ☐ |

---

## 8. Power

| # | Test | Pass criteria | ✓ |
|---:|---|---|---|
| 8.1 | Lid alone | No brownout, rail ≥ 4.6 V | ☐ |
| 8.2 | All five servos at once | No reset. Force it via self-test | ☐ |
| 8.3 | Servos + audio + LEDs full | No reset, no audio glitching | ☐ |
| 8.4 | Worst-case current | ≤ 3 A at the PSU | ☐ |
| 8.5 | Brownout counter | Zero `Brownout detector was triggered` in the log after a full session | ☐ |
| 8.6 | 30-minute soak | No resets, no thermal issues, PSU warm not hot | ☐ |
| 8.7 | Cold start ×10 | Boots reliably from cold every time | ☐ |

A single brownout message in 8.5 means the bulk capacitor is too small, too
far from the servos, or missing. Fix it before the demo, not after.

---

## 9. Pre-demo checklist

Run this in the 30 minutes before you present.

- [ ] §2 safety tests 2.1, 2.2, 2.8 re-run and passing
- [ ] Battery/power bank in the remote is charged
- [ ] `frustration` reset (leave the remote alone for 2 minutes, or reboot)
- [ ] Miss counter reset (reboot) so the escalation starts from the top
- [ ] Hidden trigger reachable from where you will be standing
- [ ] Throwables prepared: light, soft, and they must fit the opening
- [ ] Spare finger cartridge within reach
- [ ] Spare USB-C cable, and a laptop that can reflash
- [ ] Lid clear of obstructions; bin on a stable surface
- [ ] Volume set for the room
- [ ] Full run-through completed end to end at least once
