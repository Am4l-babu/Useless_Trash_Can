# REMOTE PROTOCOL

ESP-NOW link between **BIN CONTROL SYSTEM v0.0001** and **BIN-CHAD**.

Chosen over Wi-Fi/MQTT because it is peer-to-peer, needs no router, no
credentials and no association, and delivers a keypress in single-digit
milliseconds. At a hackathon the venue Wi-Fi is congested, captive-portalled,
or both. ESP-NOW does not care.

Reference: <https://developer.espressif.com/blog/2024/08/arduino-esp-now-lib/>

---

## 1. Link parameters

| | |
|---|---|
| Transport | ESP-NOW (802.11 vendor action frames) |
| Channel | 1 (`BINCHAD_ESPNOW_CHANNEL`) — **both ends pin it explicitly** |
| Encryption | None |
| Mode | `WIFI_STA`, disconnected |
| Pairing | None. The remote broadcasts; the bin answers and both then remember each other as unicast peers. |
| Device ID | `0x2A` (`BINCHAD_DEVICE_ID`) — change it to run two bins in one room |
| Latency | ~2–5 ms typical, well inside the 200 ms target |
| Range | 30 m+ line of sight; more than enough for a stage |

Both firmwares call `esp_wifi_set_channel()` at startup. Without that, the two
boards can settle on different channels and the link silently does nothing —
the single most common ESP-NOW failure.

### The shared header

`protocol.h` exists **twice, byte-identical**:

```
firmware/BinChad/src/remote/protocol.h
firmware/BinRemote/src/config/protocol.h
```

Arduino sketches cannot share files outside their own folder, so it is
duplicated rather than symlinked. **If you edit one, copy it over the other.**
A mismatch fails the checksum on every packet, and you will spend an hour
blaming the antenna.

---

## 2. Packet formats

### 2.1 Remote → Bin (`PKT_COMMAND`, 8 bytes)

```c
struct __attribute__((packed)) RemotePacket {
    uint8_t deviceId;   // 0x2A
    uint8_t version;    // 1
    uint8_t type;       // PKT_COMMAND = 0x01
    uint8_t command;    // RemoteCommand
    uint8_t sequence;   // ++ per keypress, wraps at 255
    uint8_t repeat;     // 0 = first send, 1..2 = retransmit of the same sequence
    uint8_t flags;      // bit0 held, bit1 low battery
    uint8_t checksum;
};
```

| Offset | Field | Notes |
|---:|---|---|
| 0 | `deviceId` | Rejected if it is not `0x2A`. |
| 1 | `version` | Rejected on mismatch — no negotiation, this is a hackathon. |
| 2 | `type` | Bin accepts `PKT_COMMAND` only. |
| 3 | `command` | See §3. |
| 4 | `sequence` | The de-duplication key. |
| 5 | `repeat` | Informational; the bin de-duplicates by `sequence`, not by this. |
| 6 | `flags` | `PKT_FLAG_HELD`, `PKT_FLAG_LOWBATT`. |
| 7 | `checksum` | See §2.3. |

### 2.2 Bin → Remote (`PKT_ACK`, 8 bytes)

```c
struct __attribute__((packed)) BinStatusPacket {
    uint8_t deviceId;
    uint8_t version;
    uint8_t type;        // PKT_ACK = 0x02
    uint8_t ackSequence; // the sequence being answered, 0xFF if unsolicited
    uint8_t stateId;     // PersonalityState
    uint8_t obeyed;      // 1 = the bin actually did what was asked (rare)
    uint8_t frustration; // 0..12
    uint8_t checksum;
};
```

The remote uses `obeyed` and `frustration` to drive its **COMPLIANCE %**
readout, which only ever goes down.

### 2.3 Checksum

```c
static inline uint8_t binchad_checksum(const uint8_t *b, uint8_t len) {
    uint8_t x = 0;
    for (uint8_t i = 0; i < len - 1; ++i) x ^= b[i];
    return (uint8_t)(~x);          // inverted on purpose
}
```

XOR of every byte except the last, **then inverted**. The inversion matters:
an all-zero buffer — the classic symptom of a dead link or a truncated read —
produces checksum `0xFF`, not `0x00`, so it fails validation instead of
looking like a valid `CMD_NONE`.

---

## 3. Command set

| Value | Name | What the remote means | What the bin tends to do |
|---:|---|---|---|
| 0x00 | `CMD_NONE` | — | The silent treatment (a valid outcome of mistranslation) |
| 0x01 | `CMD_UP` | eye up | usually down |
| 0x02 | `CMD_DOWN` | eye down | usually up |
| 0x03 | `CMD_LEFT` | eye left | usually right |
| 0x04 | `CMD_RIGHT` | eye right | usually left |
| 0x05 | `CMD_OK` | confirm | `OK / IS SUBJECTIVE` |
| 0x06 | `CMD_OPEN` | open the lid | closes it |
| 0x07 | `CMD_CLOSE` | close the lid | opens it |
| 0x08 | `CMD_STOP` | stop everything | everything gets faster for one second |
| 0x09 | `CMD_AI` | engage AI | 12 s of theatre, then something unrelated |
| 0x0A | `CMD_ANGRY` | be angry | is angry (this one works) |
| 0x0B | `CMD_MOOD` | change mood | picks a random personality state |
| 0x0C | `CMD_NORMAL` | normal mode | `USE THE BUTTON.` and looks at it |
| 0x0D | `CMD_MUTE` | mute | volume to maximum |
| 0x0E | `CMD_LIGHT` | lights on | lights off |
| 0x0F | `CMD_DARK` | lights off | full brightness white |
| 0x10 | `CMD_SECRET` | (BOOT button) | `YOU FOUND / NOTHING.` |
| 0x1F | `CMD_PING` | keepalive | nothing — no behaviour attached |

`CMD_PING` is sent every 1.5 s so the bin's `REMOTE LOST / Good.` message
appears only when the remote is genuinely gone, not merely idle.

---

## 4. Reliability

**Retransmission.** Each keypress is transmitted **3 times, 40 ms apart**,
with the same `sequence` and an incrementing `repeat`. ESP-NOW is unicast and
already ACKed at the MAC layer, but a remote pressed at the moment the bin's
Wi-Fi task is busy can still drop a frame.

**De-duplication.** The bin accepts a `(sequence, command)` pair once per
`REMOTE_REPEAT_MS` (250 ms). The three copies of one press therefore produce
exactly one reaction.

**Ring buffer.** The receive callback runs in Wi-Fi task context, so it does
the minimum possible: validate, push into an 8-entry ring, return. All
interpretation happens in `poll()` on the main loop. Nothing is allocated and
nothing blocks inside the callback.

**Timeout.** No valid packet for `REMOTE_TIMEOUT_MS` (4 s) sets
`remoteConnected = false`. The bin keeps working; it just becomes smug about
it. Reconnection is automatic and produces `REMOTE FOUND / Unfortunately.`

**Broadcast fallback.** Until a peer is learned, both ends send to
`FF:FF:FF:FF:FF:FF`. You never have to write down a MAC address, which means
you cannot lose the piece of paper it was written on.

---

## 5. The mistranslation engine

This is the part that makes the joke land, and the part most likely to be
mistaken for a bug. It lives in `Personality::mistranslate()`, on the **bin**,
not on the remote.

> **Why the bin and not the remote:** the remote is a truthful transmitter
> attached to a dishonest *display*. Keeping the misbehaviour in one place —
> and keeping the wire protocol honest — means you can trust the serial log
> while debugging. `[remote] asked OPEN -> doing CLOSE` tells you the link is
> perfect and the bin is simply being difficult.

### 5.1 Base weights

Per the design brief:

| Outcome | Weight | Result |
|---|---:|---|
| Correct | 10 | Does what you asked. `obeyed = true`. Followed by `...FINE.` |
| Opposite | 40 | `oppositeOf()`: UP↔DOWN, LEFT↔RIGHT, OPEN↔CLOSE, LIGHT↔DARK |
| Wrong direction | 25 | A different direction, never the requested one |
| Random action | 20 | Anything from `kAnything[]` |
| Nothing | 5 | `CMD_NONE` — refusal line, red flash |

### 5.2 Escalation

`frustration` increments on every remote command (capped at 12) and decays by
1 every 12 s, so putting the remote down for a couple of minutes resets the
demo.

```
wCorrect  = (frustration >= 6) ? 0 : (10 - frustration)
wOpposite = 40 + frustration * 2
wWrongDir = 25
wRandom   = 20 + frustration
wNothing  = 5
```

After six presses the probability of compliance is **exactly zero** and stays
there. In `P_ANGRY` the table is replaced entirely with
`correct 0 / opposite 30 / wrongDir 25 / random 45 / nothing 15`.

| Presses (= frustration) | P(correct) | P(opposite) | P(wrong dir) | P(random) | P(nothing) |
|---:|---:|---:|---:|---:|---:|
| 1 | 8.8 % | 41.2 % | 24.5 % | 20.6 % | 4.9 % |
| 3 | 6.6 % | 43.4 % | 23.6 % | 21.7 % | 4.7 % |
| 6 | **0 %** | 48.1 % | 23.1 % | 24.1 % | 4.6 % |
| 12 (cap) | **0 %** | 50.8 % | 19.8 % | 25.4 % | 4.0 % |
| *in `P_ANGRY`* | **0 %** | 26.1 % | 21.7 % | 39.1 % | 13.0 % |

(Weights are normalised over their running total, so the percentages shift as
the table grows — which is why "opposite 40" does not mean 40 %.)

### 5.3 Commands that bypass the engine

`CMD_AI`, `CMD_STOP`, `CMD_MUTE`, `CMD_MOOD`, `CMD_ANGRY`, `CMD_NORMAL` and
`CMD_SECRET` pass through untouched — their joke is implemented downstream in
`executeCommand()` rather than by substitution. Mistranslating "STOP" into
"LEFT" would be less funny than STOP making everything faster.

### 5.4 Determinism

The engine draws from the seeded xorshift32 in `Personality`, not `rand()`.
Same seed plus same sequence of presses gives the same sequence of wrong
answers. This is what makes the behaviour read as *deliberate* rather than
*broken*, and it means you can rehearse the demo and reproduce a bug.

---

## 6. Adding a command

1. Add the value to `RemoteCommand` in `protocol.h`. **Never renumber existing
   values** — the two firmwares are flashed independently and will disagree.
2. Add a case to `binchad_cmd_name()`.
3. Copy `protocol.h` to the other firmware.
4. Bin side: add a case to `BehaviorManager::executeCommand()`.
5. Decide whether it should bypass the mistranslation engine (§5.3).
6. Remote side: add it to `kButtons[]`, or to `kLadderCommands[]` plus a new
   resistor in the ladder.
7. Update the table in §3 of this document.
