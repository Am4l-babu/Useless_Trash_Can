# TRASHBOT WEB REMOTE

### A browser-based command centre for a trash can that has developed an attitude

There is no physical remote. Your phone is the remote. The ESP32 serves a
dashboard, takes commands over a WebSocket, and then decides how much of each
one it feels like honouring.

```
USEFULNESS       0%
COMPLEXITY     150%
DRAMA          200%
OBEDIENCE        4%
ENGINEERING    100%
```

This is a **separate machine** from BIN-CHAD. BIN-CHAD is an ESP32-S3 with
servos, sensors and an ESP-NOW link to a physical remote. This is a plain
ESP32 driving an L298N and two DC motors over Wi-Fi. They share no pins, no
protocol and no firmware.

The phone is also the **speaker** and the **eyes**. The bin has neither. It
sends a tiny "play this" message (over Wi-Fi, Bluetooth and USB, all at once)
and the phone plays a clip you uploaded from the AUDIO tab; the phone's
camera (or an ESP32-CAM's snapshots) runs a person-and-object detector in the
browser and tells the bin what it saw. §5 and §6 below.

---

## 1. Hardware

Everything the current build needs:

| Part | Notes |
|---|---|
| ESP32 dev module | Classic ESP32, not S3. |
| L298N motor driver | Any of the common red breakout boards. |
| 2 × DC motors | Geared, whatever the chassis takes. |
| Motor supply | 6–12 V into the L298N's `+12V`. **Not** the ESP32's 5 V pin. |

### Wiring

The pin map is the one already proven on the bench by
`tests/l298n_motor_test` — if you have run that sketch, you are already wired.

| L298N | ESP32 | Purpose |
|---|---|---|
| `ENA` | GPIO 18 | left motor speed (PWM) |
| `ENB` | GPIO 19 | right motor speed (PWM) |
| `IN1` | GPIO 27 | left direction |
| `IN2` | GPIO 26 | left direction |
| `IN3` | GPIO 25 | right direction |
| `IN4` | GPIO 33 | right direction |
| `GND` | `GND` | **mandatory** — the common ground |
| `+12V` | motor supply | external, never the ESP32 |

Remove the L298N's 5 V regulator jumper if you are feeding it more than 12 V,
and do not try to power the ESP32 from the L298N's 5 V output while the motors
are stalling — the brownout will reboot the board mid-drive.

> If a motor does not spin at all, check the common ground first. It is the
> reason nine times out of ten.

---

## 2. Build and flash

### Libraries

| Library | Version |
|---|---|
| `bblanchon/ArduinoJson` | 7.x (the v7 `JsonDocument` API) |
| `esp32async/AsyncTCP` | 3.x |
| `esp32async/ESPAsyncWebServer` | 3.x |
| `h2zero/NimBLE-Arduino` | 2.x (the Bluetooth trigger link; set `TRASHBOT_BLE 0` in `settings.h` to leave it out) |

### PlatformIO (what this was built and verified with)

```bash
cd firmware/TrashBotWeb
pio run              # build
pio run -t upload    # flash the firmware
pio run -t uploadfs  # flash data/ to LittleFS  <- do not skip this
```

The partition table is `no_ota.csv` (2 MB app, 1.9 MB filesystem): Wi-Fi plus
the async web server plus BLE is about 1.2 MB of code, and the sound clips
need room. Flashing this over a board that had the default layout **erases
the old filesystem**, so run `uploadfs` again afterwards.

### Arduino IDE

Open `TrashBotWeb.ino`. Board: **ESP32 Dev Module**, Partition Scheme:
**No OTA (2MB APP/2MB SPIFFS)**. Install the four libraries above through
the Library Manager, then use **Tools → ESP32 Sketch Data Upload** to write
`data/` to LittleFS.

**Both steps are needed.** The firmware holds a minimal fallback page in
PROGMEM, so a board with an empty filesystem still serves a working four-button
control pad and an emergency stop rather than a blank screen — but the real
dashboard lives in `data/`.

### First run

With no stored Wi-Fi credentials the board brings up its own access point:

```
SSID      TRASHBOT-SETUP
password  uselessbin          (change it in settings.h or the SETUP tab)
open      http://192.168.4.1
```

Enter your network in the **SETUP** tab and it restarts onto it. After that:

```
http://trashcan.local
```

The serial port prints the address either way, at 115200 baud.

---

## 3. Using it

**Hold to drive.** The joystick and the D-pad are dead-man's switches. While
held, the browser repeats your intent every 100 ms; when you let go it sends a
stop. If the browser goes quiet for 450 ms the ESP32 stops the motors on its
own, and if the WebSocket drops it stops them immediately. The browser is
never the only thing keeping the robot safe.

This also means a *delayed* command only lands if you are still holding the
control when the bin finally gets round to it. That is the joke working as
intended.

### The buttons

| Button | What it does |
|---|---|
| Joystick / D-pad | Drive. Eight directions plus stop. |
| STOP | Stops. Always. See §4. |
| SPEED | What you would like. Not necessarily what you get. |
| OPEN / CLOSE | Lid commands. No servo is fitted, so the lid state is labelled **SIMULATED**. |
| PLEASE | Raises obedience and trust a little. Response: *"Maybe."* |
| SORRY | Reduces anger — with diminishing returns. Apologise too often and you get *"Too late."* |
| PANIC | Theatrical. Raises anger and confusion, flips the face, expires after 9 s. Moves nothing by itself. |
| DO NOTHING | Does nothing, with a full report. Never transformed. |
| RETURN TO NORMAL | Grants genuine normal behaviour for 0.6–4.5 s, then takes it away again. |
| EMERGENCY STOP | Real. See §4. |

### Drive modes

| Mode | Behaviour |
|---|---|
| `NORMAL` | Genuine debug mode. Identity transform, zero delay, permanent. |
| `UNCOOPERATIVE` | The default show. Weighted misbehaviour driven by the traits. |
| `REVERSE` | Deterministic inversion. Predictable, and your own fault. |
| `CHAOS` | Seeded pseudo-random mapping that changes between presses. |
| `DRUNK` | Mostly tries, with a wandering steering bias and delays. |
| `LAZY` | Obeys slowly. Acceleration drops to a crawl. |
| `ANGRY` | Zero compliance, mostly random substitutions. |
| `SLEEP` | Ignores about three commands in four. |
| `PANIC` | Short delays, high randomness, maximum drama. Auto-expires. |

### Personality

Seven traits — anger, trust, happiness, confusion, boredom, obedience,
rebellion — drift over time and react to what you do. Mashing the same
direction raises anger. Leaving it alone raises boredom. Rebellion is derived
from anger and obedience rather than stored independently, so the readout can
never disagree with the behaviour.

Once anger passes 70, the probability of compliance is **exactly zero** and
stays there until it decays.

Behaviour is driven by one seeded xorshift32 stream, printed at boot:

```
[    1243] BOOT   personality seed 0x9F3A1C22 mood NORMAL
```

Same seed plus the same presses gives the same sequence of wrong answers. Pin
it in NVS (key `seed` in namespace `trashbot`) to rehearse a demo or reproduce
a complaint. Zero means "surprise me".

---

## 4. Safety

The one rule: **safety runs before the personality engine, and the personality
engine cannot reach back into it.** It cannot clear the emergency stop, extend
a timeout, raise a speed limit, or delay a stop.

Four independent inhibits:

| Inhibit | Trigger | Effect |
|---|---|---|
| Emergency stop | The red button | Latched. Motors cut. Cleared only by an explicit reset. |
| No clients | Browser closed | Motors cut immediately. |
| Link timeout | 1500 ms of silence | Motors cut. |
| Drive timeout | 450 ms without drive intent | Target ramped to zero. |

Below all of that, the motor layer enforces a maximum duty and an acceleration
limit, and deceleration is always allowed to outrun acceleration.

The emergency stop is latched in the network callback the instant the frame
arrives, without waiting for the main loop, and requires a deliberate
**RESET SAFETY SYSTEM** press to clear.

---

## 5. Sound: the phone is the speaker

There is no speaker on the board and no audio data ever leaves it. When
something happens, the firmware sends a message naming the event and the
clip, and every phone with the dashboard open plays that clip. This makes
the phone in your hand the bin's voice, which is the joke: it talks to you
through the thing you are trying to control it with.

**AUDIO tab, top to bottom:**

1. **ENABLE SOUND ON THIS DEVICE.** Browsers will not play audio until you
   have tapped something. Tap this once per visit. The bin knows whether any
   phone has done so (the `AUDIO` box on the HW tab goes green).
2. **Trigger links.** Wi-Fi is the WebSocket the page already uses and is
   always on. Bluetooth and USB are optional extras, below.
3. **Clip library.** Upload MP3, AAC, OGG or WAV, up to 512 KB each. Clips
   are stored on the bin's flash under `/audio/` so every phone gets the same
   set, and each phone keeps a decoded copy in IndexedDB so it can still
   play them with no Wi-Fi at all. Names are lower-cased and cleaned:
   `Hi Chellam I Love u.wav` becomes `hi_chellam_i_love_u.wav`.
4. **What plays when.** One row per event, 31 of them: a person detected,
   a person gone, an object spotted, each command verdict, the emergency
   stop, every button, every mood change. Pick a clip from the dropdown;
   it saves immediately. **TEST** fires the real event through the bin so
   every connected phone plays it, exactly as it will during the demo.

The two clips in the repo's `audio/` folder ship in the filesystem image,
with *"Hi Chellam I love you"* assigned to **Human detected** and the AAC
one to **Phone connected**. Change them from the tab; nothing is hard-wired.

Events have a cooldown (1.5 s per event, 250 ms between any two) so a held
joystick does not produce a stutter of "obeyed". The emergency stop ignores
the cooldown. If no clip is assigned to an event, nothing is sent at all.

### The three links

| Link | What it carries | Where it works |
|---|---|---|
| **Wi-Fi** | Sound messages, commands, telemetry, the dashboard itself | Everywhere. Always on while the page is open. |
| **Bluetooth** | Sound messages, and the same JSON commands (the bin drives with Wi-Fi off) | Chrome on Android, ChromeOS, Windows, macOS. Not iOS. |
| **USB** | Sound messages on the serial port, JSON commands back | Web Serial on a laptop. WebUSB on Android with a CP2102 or CH340 board, via a small driver in `audio.js`. |

All three carry the same sequence number, so a phone on Wi-Fi and Bluetooth
at once plays each sound once. The Bluetooth payload is `seq;eventId;clip`
rather than JSON because a phone that never negotiated a bigger MTU only
sees the first 20 bytes, and that is enough to look the clip up locally.

**The catch, stated plainly:** Bluetooth, USB and the phone's own camera are
browser APIs that require a *secure* page, and `http://192.168.4.1` is not
one. Chrome will allow it if told to, once per device: open
`chrome://flags/#unsafely-treat-insecure-origin-as-secure`, enter the bin's
address, set **Enabled**, relaunch. The tab shows this exact instruction
when it applies. Wi-Fi needs none of it. The Bluetooth and USB links were
written against the documented Web Bluetooth, Web Serial and WebUSB APIs and
the CP210x/CH341 register maps; they have **not** been exercised on a phone
in this environment — the Wi-Fi link, the de-duplication and both message
formats have (see §10).

---

## 6. Vision: the phone is the eyes

No detector runs on the ESP32. It could not, and it does not pretend to. The
VISION tab runs **COCO-SSD under TensorFlow.js in the browser** — people
plus 80 everyday objects (cup, bottle, phone, banana, the usual) — on one of
two frame sources, and sends only the *result* to the bin:

| Source | How | Needs |
|---|---|---|
| **This device's camera** | `getUserMedia`, rear camera | The secure-page flag above (it is a camera API). |
| **ESP32-CAM snapshots** | Polls `http://trashcam.local/capture` a few times a second | The companion board in `firmware/TrashBotCam/`, which joins the bin's AP and serves JPEGs with permissive CORS. No flag. |

Either way the model (~6 MB) comes from a CDN the first time and lives in the
browser cache after that. On the bin's own access point the phone has no
internet unless it keeps mobile data on, so **open the VISION tab once at
home** before the demo.

What the bin does with a report:

- A person **arriving** logs `HUMAN_DETECTED`, fires the `human_detected`
  sound, and cheers the personality up (happiness up, boredom down). A person
  standing there is not news. A person gone for 2.5 s logs `HUMAN_LOST`.
- An object it has not announced in the last 8 s logs `OBJECT_DETECTED cup`,
  fires `object_detected`, and confuses it slightly.
- A source that goes quiet for 5 s is dropped and the telemetry says so;
  stale numbers are never shown as current.
- Nothing here can move a motor. Detection is an input to the personality,
  like PLEASE and SORRY, and goes nowhere near the safety layer.

Reports can also arrive from a camera board directly: `POST /api/vision`
over Wi-Fi, or a text line on UART2 (`VISION persons=1 objects=cup conf=83`
at 115200 baud on GPIO 16 RX / 17 TX). The bin does not care who ran the
detector; it tells you the source in telemetry.

---

## 7. Decisions that differ from the brief

Each of these is a place where following the brief literally would have meant
either lying to the user or building something unsafe.

**1. STOP always stops.** The brief allows an angry bin to answer `NO.` to
STOP. It does — on screen. The motors stop anyway, every time. STOP is never
transformed, delayed or ignored. "It would not stop" is not a joke, it is a
fault report.

**2. Hold to drive.** A tap does not latch a direction. A robot that keeps
driving after you let go of the control is a hazard, and the dead-man's switch
is also what makes the delayed-command gag land honestly.

**3. PANIC does not move the motors by itself.** It is a mood and a light
show. Autonomous movement that the user did not ask for is the one kind of
surprise this project does not make.

**4. The battery readout says `NOT INSTRUMENTED`.** There is no divider on
this board, so there is no voltage to report. The brief's mock-up shows
`BATTERY 87%`; inventing that number would break the project's own rule about
never faking a sensor value.

**5. Nothing claims to be `DETECTED`.** The L298N and the motors are passive
and cannot be sensed on these pins, so the hardware page says `CONFIGURED` —
the firmware is driving those pins — and reserves `ONLINE` for states that are
genuinely observable, like the Wi-Fi link and the client count.

**6. There are two normal modes, on purpose.** The big green button is the
gag: it expires. The mode selector's `NORMAL` entry is a real, permanent
debug mode, because a machine this deliberately unreliable needs one honest
setting to debug against. The button lies; the settings do not.

---

**7. The phone is the speaker and the camera.** The brief wants the bin to
talk and to see. An ESP32 with an L298N can do neither, so both jobs go to
the phone that is already in the loop, and the hardware page says
`BORROWED FROM THE PHONE` rather than `CONFIGURED` for those two rows. What it
*does* report as its own is whether a phone is currently doing the job.

---

## 8. Calibration

**SETUP → MOTOR CALIBRATION.** Jog each motor forward and reverse; the jog
runs for at most two seconds and stops itself.

1. If a motor turns the wrong way, tick its **INVERT** (or swap its two `OUT`
   wires — equally valid).
2. **MIN DUTY** is the lowest duty that actually turns a loaded motor.
   Below it a DC motor buzzes and heats instead of moving, so any non-zero
   request is lifted to this value. Raise it until "slow" means slow rather
   than stalled.
3. **MAX DUTY** is the ceiling everything else is clamped to. Nothing the
   personality engine does can exceed it.
4. **ACCELERATION** is duty per 20 ms tick.
5. **STRAIGHTNESS TRIM** only ever takes power away from the faster side, so
   it cannot be used to exceed the ceiling.
6. **SAVE TO NVS** — settings survive a reflash.

---

## 9. Architecture

```
        PHONE / LAPTOP  (dashboard, speaker, detector)
          |  Wi-Fi WebSocket      ^  BLE notify / USB serial line
          v                      |
   +-------------------------+   |        ESP32-CAM (optional)
   |          ESP32          |   |        /capture JPEGs -> the phone
   |  webLayer   (AsyncTCP)  |   |
   |  bleLink / usbLink      |---+   same validate -> ring buffer path
   |  pipeline   (loop)      |   the command's journey
   |  personality            |   decides what to do instead
   |  vision                 |   what was seen, whether it is news
   |  soundBank              |   which clip, rate limited, one queue
   |  safety                 |   has the last word
   |  motors                 |   ramp, clamp, L298N
   +-----------+-------------+
               v
             L298N -> two motors
```

Every command takes exactly this path:

```
WEB BUTTON -> INPUT VALIDATION -> SAFETY CHECK -> PERSONALITY ENGINE
 -> COMMAND TRANSFORMATION -> OPTIONAL DELAY -> MOTOR CONTROLLER
 -> TELEMETRY -> WEB UI
```

| File | Contents |
|---|---|
| `TrashBotWeb.ino` | Wi-Fi, mDNS, and the loop that orders everything below. |
| `src/config/pins.h` | GPIO map. |
| `src/config/settings.h` | Every tunable number, plus the hardware inventory. |
| `src/core/types.h` | The vocabulary every module shares. |
| `src/core/prng.h` | The seeded xorshift32. The only randomness in the build. |
| `src/core/eventLog.*` | Ring buffer behind the on-screen log and the serial trace. |
| `src/motor/motors.*` | L298N, ramping, clamping, NVS calibration. |
| `src/safety/safety.*` | Latch, timeouts, and `enforce()`. |
| `src/personality/personality.*` | Traits, moods, the transformation table. |
| `src/command/pipeline.*` | The pipeline above, including the non-blocking delay. |
| `src/sound/soundBank.*` | The event catalogue, the event → clip map (JSON on LittleFS), cooldowns, the outbound queue. |
| `src/vision/vision.*` | Detection intake from WebSocket, HTTP and UART2; arrival/departure logic. |
| `src/net/webLayer.*` | HTTP, WebSocket, JSON, the audio API, the fallback page. |
| `src/net/bleLink.*` | NimBLE service: sound notifications out, JSON commands in. |
| `src/net/usbLink.*` | `SND {...}` lines out on the serial port, JSON lines in. |
| `data/` | The dashboard. Plain HTML/CSS/JS, no build step. `audio.js` is the speaker, `vision.js` the eyes. |
| `data/audio/` | Seed clips and `map.json`, packed into the filesystem image. |

Three tasks exist. The WebSocket callback (AsyncTCP task) and the BLE write
callback (NimBLE host task) are allowed to do three things only: reject
oversized frames, parse and validate JSON, and push a fixed-size struct into
a spinlocked ring. The audio upload handler additionally writes a file.
Everything that acts on a command happens in `loop()`. The emergency stop is
the single deliberate exception — it latches on arrival.

There is no `delay()` after `setup()` returns.

The WebSocket message format is documented in [PROTOCOL.md](PROTOCOL.md).

---

## 10. Verified how

`pio run` builds clean with `-Wall` (the only warning is inside NimBLE
itself) at 58 % of the 2 MB app partition and 20 % static RAM; the LittleFS
image packs with the seed clips. The dashboard was driven in headless Chrome
against a Python mock of this firmware's HTTP and WebSocket surface: page
load with no JS errors, the clip library and all 31 event rows, decoding of
the WAV and AAC seeds, unlock, a `sound` message playing the mapped clip, the
same sequence number arriving in BLE form being ignored, a truncated BLE
payload resolving through the map, the USB line form, the ping advertising
sound, a multipart upload with name sanitising, assignment, TEST through the
bin, delete with unassignment, and the VISION tab loading COCO-SSD and
reporting from a (fake) camera at ~4 detections/s. Not verified: any of it on
a real ESP32, a real phone's Bluetooth or USB, or a real ESP32-CAM.

---

## 11. Adding the hardware that is not there yet

The inventory flags at the bottom of `settings.h` are the seam:

```c
static const bool HW_LID_SERVO = false;
```

Flip one to `true` at the same time as you add its driver module and its pin
`#define`. The UI stops saying `NOT INSTALLED`, stops labelling that
subsystem `SIMULATED`, and starts showing it as `CONFIGURED`. Nothing else
has to change — the OPEN/CLOSE buttons, the event codes and the telemetry
fields already exist and already run through the personality engine.

Pins are reserved but deliberately not `#define`d in `pins.h`: an undefined
pin cannot be driven by accident.

---

## 12. When it does not work

| Symptom | Cause |
|---|---|
| Blank page, or a page saying "minimal" | `data/` was never uploaded. Run `pio run -t uploadfs`. |
| Clips vanished after a reflash | The partition table changed under them. `uploadfs` again, or re-upload from the tab. |
| No sound, `SOUND LOCKED` on the AUDIO tab | Nobody tapped ENABLE SOUND on that phone. Browsers insist. |
| A sound plays on one phone but not another | Each phone unlocks separately. The bin sends to all of them. |
| `CANNOT DECODE` next to a clip | That phone's browser cannot play the format. WAV and MP3 work everywhere; AAC nearly everywhere. |
| Bluetooth / USB button says `NEEDS SECURE PAGE` | The `chrome://flags` step in §5, once per device. |
| VISION says `could not start: camera needs a secure page` | Same flag, or use the ESP32-CAM source, which needs no flag. |
| VISION stuck at `MODEL downloading…` | No internet on the phone. Load it once at home; it caches. |
| `SNAPSHOT failed` with the ESP32-CAM source | Wrong URL, the cam is not on the same network, or its ribbon cable is loose (its serial log says). |
| `trashcan.local` does not resolve | Android does not do mDNS well. Use the IP from the serial port. |
| Connects, then `OFFLINE` every few seconds | Weak Wi-Fi. The link timeout is doing its job. Move closer or use the AP fallback. |
| Motors buzz but do not turn | `MIN DUTY` too low, or the motor supply is sagging. |
| Both motors run backwards | Two `INVERT` ticks, or swap both motors' `OUT` pairs. |
| It drives the wrong way on purpose | Working as designed. Check the event log — every transformation is logged with its reason. |
| It genuinely will not obey in `NORMAL` mode | That is a bug. `NORMAL` is the honest mode. Check the seed line and the log. |

The log is the diagnostic. `COMMAND_MODIFIED FORWARD -> BACKWARD (Because I
can.)` means the link is perfect and the bin is simply being difficult.
