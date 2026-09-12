# WEBSOCKET PROTOCOL

Link between a browser and **TRASHBOT OS**.

| | |
|---|---|
| Transport | WebSocket, `ws://<host>/ws` |
| Encoding | JSON, UTF-8, one message per frame |
| Frame limit | 512 bytes inbound — anything larger is refused without being parsed |
| Fragmentation | Not supported. Every message this protocol defines fits in one frame. |
| Clients | Up to 4 at once. They all see the same telemetry and the same log. |

Chosen over HTTP polling because the dashboard needs sub-100 ms feedback in
both directions, and because a persistent connection is itself the liveness
signal the safety layer uses: if the socket drops, the motors stop.

Messages arriving on the socket are handled in the AsyncTCP task, which
validates them and pushes a fixed-size struct into a ring buffer. Nothing
acts on a command outside `loop()`. The one exception is `estop`, which
latches the moment it is parsed.

---

## 1. Browser → bin

Every message has a `type`. Unknown types are dropped in silence; malformed
JSON is dropped and logged as `COMMAND_REJECTED`. Every field is bounded on
arrival — there is no path by which a browser can ask for a duty above the
configured ceiling.

### 1.1 `ping`

```json
{ "type": "ping", "snd": true }
```

Sent every 400 ms. Any inbound message refreshes the link timer, so a stream
of `drive` messages serves the same purpose; the ping exists so that an idle
browser still counts as present.

`snd` is true when that browser has unlocked audio playback. It is the only
way the bin can know it has a speaker; `telemetry.audioReady` reflects it for
10 s after the last such ping.

**Contract:** 1500 ms with no inbound message of any kind is a link timeout
and cuts the motors.

### 1.2 `drive`

Two forms. Discrete, from the D-pad:

```json
{ "type": "drive", "dir": "forward", "speed": 60 }
```

Analogue, from the joystick:

```json
{ "type": "drive", "x": -42, "y": 88, "speed": 60 }
```

| Field | Range | Notes |
|---|---|---|
| `dir` | see below | Takes precedence over `x`/`y` if present. |
| `x`, `y` | −100..100 | `y` is positive forward. Quantised to one of eight sectors. |
| `speed` | 0..100 | Percent. In the analogue form it is scaled by stick deflection. |

Direction wire names:

```
stop  forward  backward  left  right
fwd_left  fwd_right  back_left  back_right
```

A vector with a magnitude under 20 is a `stop`.

**Contract:** repeat every 100 ms while the control is held, and send
`{"type":"drive","dir":"stop"}` on release. 450 ms without drive intent
ramps the motors to zero. A `stop` also cancels any command still waiting out
its delay.

### 1.3 `action`

```json
{ "type": "action", "action": "please" }
```

One of `open`, `close`, `please`, `sorry`, `panic`, `do_nothing`,
`normal_mode`.

### 1.4 `mode`

```json
{ "type": "mode", "mode": "chaos" }
```

One of `normal`, `uncooperative`, `reverse`, `chaos`, `drunk`, `lazy`,
`angry`, `panic`, `sleep`. Changing mode cancels any pending delayed command.

### 1.5 `speed`

```json
{ "type": "speed", "value": 60 }
```

The slider's resting value, clamped to 10..100. Sent on release rather than
during the drag. Individual `drive` messages carry their own `speed`.

### 1.6 `estop` and `reset_safety`

```json
{ "type": "estop" }
{ "type": "reset_safety" }
```

`estop` cuts the motors and latches, in the network task, before the main loop
runs again. No mode, mood or trait value can influence it.

`reset_safety` is the only thing that clears the latch. Nothing moves until a
fresh drive command arrives afterwards.

### 1.7 `cal_jog` and `cal_set`

```json
{ "type": "cal_jog", "motor": "left", "dir": "forward" }
{ "type": "cal_set", "field": "max_duty", "value": 200 }
```

`motor` is `left` or `right`; `dir` is `forward`, `reverse` or `stop`. A jog
runs one motor for at most 2000 ms and then stops itself. Jogs are refused
while the emergency stop is latched.

`cal_set` fields:

| Field | Range | Meaning |
|---|---|---|
| `left_invert` | 0/1 | Flip the left motor's direction. |
| `right_invert` | 0/1 | Flip the right motor's direction. |
| `max_duty` | 40..255 | Ceiling. Everything is clamped to this. |
| `min_duty` | 0..`max_duty`−1 | Non-zero requests are lifted to this. |
| `accel` | 1..60 | Duty change per 20 ms tick while speeding up. |
| `trim` | −20..20 | Straightness. Positive slows the left motor. |
| `save` | — | Commit the current values to NVS. |

Changes apply immediately and are lost on reboot until `save`.

### 1.8 `vision`

```json
{ "type": "vision", "persons": 1, "objects": "cup,bottle", "conf": 83, "src": "phone" }
```

A detection result from whoever ran the detector. `persons` 0..255,
`objects` a comma-separated list of class names (`person` is ignored here;
it is counted, not listed), `conf` 0..100 is the best score in the frame,
`src` is `phone` (this device's camera) or `cam` (ESP32-CAM snapshots).

Send on every change and at least once a second while running: a source
that is silent for 5 s is dropped. Send `persons: 0` when stopping rather
than letting it time out. The bin decides what is news — see §2.6 and the
README — and nothing in this message can move a motor.

### 1.9 `sound_test`

```json
{ "type": "sound_test", "event": "estop" }
```

Fire that event's clip on every link, cooldowns bypassed. The AUDIO tab's
TEST button. Logged as `AUDIO_TEST`; if nothing is assigned it says so.

---

## 2. Bin → browser

### 2.1 `hello`

Sent once per client, on connect.

```json
{
  "type": "hello",
  "version": "0.1.7",
  "seed": "0x9F3A1C22",
  "pingMs": 400,
  "driveRepeatMs": 100,
  "driveTimeoutMs": 450,
  "hw": {
    "esp32": "online", "l298n": "configured",
    "motorL": "configured", "motorR": "configured",
    "lid": "not_installed", "camera": "see_telemetry", "speaker": "see_telemetry",
    "ble": "online", "usb": "configured", "visionUart": "configured",
    "battery": "not_instrumented"
  },
  "ble": {
    "name": "TRASHBOT",
    "service": "7a5b0001-8e6f-4c1d-9b2a-3f4e5d6c7b8a",
    "sound":   "7a5b0002-8e6f-4c1d-9b2a-3f4e5d6c7b8a",
    "command": "7a5b0003-8e6f-4c1d-9b2a-3f4e5d6c7b8a"
  }
}
```

`hw` values are `online`, `configured`, `not_installed`, `not_instrumented`
or `see_telemetry`. **`online` is only used where the state is genuinely
observable.** Passive parts that cannot be sensed on these pins report
`configured`, meaning the firmware is driving those pins — not that anything
was detected. Nothing here is auto-detected. `see_telemetry` marks the two
parts the bin borrows from the phone (camera, speaker): whether a phone is
doing that job right now is observable and lives in `telemetry.vision` and
`telemetry.audioReady`. `ble` is `online` once advertising started,
`failed` if the radio did not come up, `not_installed` if compiled out.

The `ble` block is the GATT layout, so the browser does not hard-code it
(§4).

`seed` is the personality PRNG seed. Behaviour is reproducible for a given
seed.

### 2.2 `config`

Sent on connect and after any calibration change.

```json
{ "type": "config",
  "cal": { "left_invert": false, "right_invert": false,
           "max_duty": 200, "min_duty": 70, "accel": 12,
           "trim": 0, "duty_ceiling": 255 } }
```

### 2.3 `telemetry`

Broadcast at 10 Hz while at least one client is connected.

```json
{
  "type": "telemetry",
  "mode": "CHAOS", "mood": "ANGRY", "modeId": 3, "moodId": 4,
  "traits": { "anger": 82, "trust": 18, "happiness": 41,
              "confusion": 30, "boredom": 52, "obedience": 4,
              "rebellion": 91 },
  "leftMotor": -70, "rightMotor": 70,
  "leftTarget": -70, "rightTarget": 70,
  "speedReq": 60,
  "estop": false, "estopReason": "", "inhibit": "",
  "clients": 1, "uptime": 123456, "heap": 184320, "rssi": -54,
  "pendingCmd": false, "normalGrace": false, "jog": false,
  "lid": { "installed": false, "open": false, "simulated": true },
  "battery": null,
  "bleClients": 0,
  "vision": { "source": "phone", "live": true, "persons": 1, "present": true,
              "objects": "cup,bottle", "conf": 83, "humans": 3, "things": 7 },
  "audioReady": true
}
```

Motor values are percentages of the configured `max_duty`, signed. `Target`
is what was asked for; the unsuffixed value is where the ramp has actually
got to.

`inhibit` is why the motors are being held at zero, if they are:
`EMERGENCY STOP`, `NO CLIENT`, `LINK TIMEOUT`, or empty. `clients` counts
WebSocket browsers; `bleClients` counts Bluetooth phones. Both count for the
`NO CLIENT` rule.

**`battery` is `null` and stays `null`** until a divider exists to measure it.
A number here would be a fabricated sensor reading.

`vision` is the bin's own view of the last report: `source` is `phone`,
`cam`, `uart`, `http` or `none`; `live` false with a source name means that
source went quiet and the numbers are stale; `present` is the debounced
"someone is here" (a person must be gone 2.5 s to clear it); `humans` and
`things` are lifetime arrival counts. `audioReady` is whether any phone has
said it can play sound in the last 10 s.

### 2.4 `event`

One per log entry, plus a replay of the last 16 on connect.

```json
{ "type": "event", "seq": 412, "ms": 102341,
  "code": "COMMAND_MODIFIED", "text": "FORWARD -> BACKWARD (Because I can.)" }
```

`seq` is monotonic, so a client can tell whether it missed anything.

Codes in use:

```
BOOT                 NET_UP               NET_CONFIG
USER_CONNECTED       USER_DISCONNECTED    QUEUE_FULL
COMMAND_RECEIVED     COMMAND_EXECUTED     COMMAND_MODIFIED
COMMAND_REJECTED     COMMAND_CANCELLED    ACTION
SPEED_SET            MODE_CHANGED         MOOD_CHANGED
LID_OPENED           LID_CLOSED           PANIC
NORMAL_MODE          NORMAL_MODE_DISABLED
SAFETY_STOP          SAFETY_RESET
CAL_JOG              CAL_JOG_END          CAL_SAVED
AUDIO                AUDIO_ADDED          AUDIO_REMOVED
AUDIO_MAPPED         AUDIO_TEST
VISION               HUMAN_DETECTED       HUMAN_LOST
OBJECT_DETECTED
```

`COMMAND_REJECTED` means validation or the safety layer refused it.
`COMMAND_CANCELLED` means a pending command was dropped. Neither is a joke —
keeping them distinct from the comedic `IGNORED` verdict is what lets the log
tell "deliberately unhelpful" apart from "actually broken".

### 2.5 `command`

The feedback for one button press. Sent up to twice: once on arrival if the
bin is sitting on it, and once when it resolves.

```json
{
  "type": "command", "kind": "drive",
  "requested": "FORWARD", "actual": "BACKWARD",
  "verdict": "MODIFIED", "delayMs": 1700,
  "speedReq": 60, "speedAct": 17,
  "reason": "Because I can.", "quip": "I optimized it.",
  "phase": "done", "simulated": false
}
```

| Field | Values |
|---|---|
| `kind` | `drive` or `action` |
| `verdict` | `OBEYED`, `MODIFIED`, `IGNORED`, `REJECTED` |
| `phase` | `received` (delay started), `executing`, `done` |
| `reason` | The WHY panel's text. Empty when it simply obeyed. |
| `simulated` | The target hardware is not fitted, so nothing physical happened. |

A `received` phase with a non-zero `delayMs` is the bin telling you it has
heard you and is choosing not to act yet. If the control is released before
the delay expires, the command is dropped and a final `done` arrives with
`reason: "You let go."`

### 2.6 `sound`

```json
{ "type": "sound", "seq": 12, "event": "human_detected",
  "label": "Human detected", "clip": "hi_chellam_i_love_u.wav" }
```

"Play this." The bin sends no audio, ever: the browser already holds the
clip (fetched from `/audio/<clip>` and decoded) and plays it. `seq` is
shared with the BLE and USB copies of the same message (§4, §5) so a phone
on two links plays once. Only sent when a clip is assigned to the event and
the event's cooldown allows it (1.5 s per event, 250 ms between any two;
`estop` bypasses both).

### 2.7 `audio_changed`

```json
{ "type": "audio_changed" }
```

The clip library or the map changed (an upload, a delete, an assignment,
from any phone). Refetch `GET /api/audio`.

---

## 3. HTTP

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | The dashboard from LittleFS, or the built-in fallback page. |
| `/ws` | GET | WebSocket upgrade. |
| `/audio/<name>` | GET | A clip, `Cache-Control: max-age=86400`. The browser appends `?v=<size>` so a replaced clip is refetched. |
| `/api/audio` | GET | The library: `{"clips":[{"name","size"}],"map":{event:clip},"events":[{"n","id","label","desc"}],"fs":{"used","total"},"maxClip"}`. `map` has every event key; `""` means unassigned. |
| `/api/audio` | POST | Multipart upload of one clip. The filename is sanitised (lower-case, `[a-z0-9._-]`, spaces to `_`, audio extension required, ≤ 39 chars, ≤ 512 KB). 400 with a reason on refusal. |
| `/api/audio?name=x` | DELETE | Remove a clip. Any event using it becomes unassigned. |
| `/api/audio/map` | POST | Form `event=<id>&clip=<name>`; empty `clip` unassigns. 404 if the clip does not exist. Saved to `/audio/map.json`. |
| `/api/vision` | POST | Form `persons`, `objects`, `conf`, `src` — the same report as §1.8, for a camera board that would rather not open a WebSocket. |
| `/api/wifi` | POST | Form-encoded `ssid` and `pass`. Stored in NVS; the board restarts. |

`/api/wifi` is reachable over the AP fallback, which is the only way to
configure a board that has never been on a network. It is not
authenticated beyond the AP's own WPA2 password — the same trust boundary as
the motor controls it sits next to. The audio routes share that boundary:
anyone who can drive the bin can change what it says.

---

## 4. Bluetooth Low Energy

Device name `TRASHBOT`, advertising the service UUID. One service, two
characteristics; the UUIDs are also in the `hello` message.

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| sound | `7a5b0002-…` | READ, NOTIFY | `seq;eventNumber;clip` e.g. `12;3;hi_chellam_i_love_u.wav` |
| command | `7a5b0003-…` | WRITE, WRITE_NR | Any §1 message, as JSON, ≤ 512 bytes |

The sound payload is not JSON because a phone that never negotiated a
larger MTU sees only the first 20 bytes. `seq;eventNumber` always fits;
`eventNumber` is `n` from `/api/audio`'s event list, and the browser looks
the clip up in the map it already holds if the name was cut off. The bin
requests an MTU of 247.

Writes go through the same validate-and-queue path as WebSocket frames. A
connected phone counts as a client for the `NO CLIENT` rule and its
silence trips the 1500 ms link timeout like anyone else's, so a phone that
is driving over Bluetooth must also ping over Bluetooth. Up to 3 connections.

---

## 5. USB serial

115200 baud, the same port as the boot log. One line per sound, prefixed so
a reader can skip everything else:

```
SND {"type":"sound","seq":12,"event":"human_detected","clip":"hi_chellam_i_love_u.wav"}
```

Any inbound line that starts with `{` is handed to the same validator as
the WebSocket, so a laptop on Web Serial or a phone on WebUSB can drive the
bin and hear it with no radio at all. A serial peer cannot be detected, so
one counts as a client only while it has sent a JSON line inside the
1500 ms link timeout — keep pinging, as over any other link.

---

## 6. UART2 (camera board)

`PIN_VISION_RX` GPIO 16, `PIN_VISION_TX` GPIO 17, 115200 8N1, 3.3 V. One
report per line, keys in any order, unknown keys ignored, lines not
starting with `VISION` dropped (a camera board's own debug output is fine
on the same wire):

```
VISION persons=1 objects=cup,bottle conf=83
```

---

## 7. Adding a command

1. Add the value to the relevant enum in `src/core/types.h`, at the end.
   Do not renumber existing values.
2. Add its wire spelling to the matching `parse*()` in `src/net/webLayer.cpp`,
   and to `dirName()` / `actionName()` in `types.h` if it needs a display name.
3. Add a case to `Pipeline::handle()`.
4. Decide whether the personality engine may transform it. If it can stop a
   motor or is labelled as doing nothing, it must bypass the engine — see
   `ACT_DO_NOTHING` and `DIR_STOP` for the two existing precedents.
5. Add the button to `data/index.html` and its handler to `data/app.js`.
6. Update §1 or §2 of this document.
