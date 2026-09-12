# HACKATHON DEMO

Two minutes. Four beats. One punchline.

The rule that governs everything below: **the audience must understand the
joke without your explanation.** If you have to narrate what the bin is doing,
the machine has failed, not the script. Talk less than you think you should.

---

## 1. Setup, before they arrive

| | |
|---|---|
| Bin position | Chest height if possible, on a stable table, lid clear |
| Your position | Beside the bin, hidden trigger within reach |
| Throwables | 3–4 light, soft, bin-sized items. Paper balls are ideal — a crumpled sheet is visible, harmless, and forgiving |
| Remote | On the table, in view, **not** in your hand yet |
| Power | Wall socket, not a battery. Cable taped down |
| Volume | Set for the actual room, with people in it |
| State | **Reboot immediately before.** Fresh miss counter, zero frustration |

That last one matters. If you have been testing all morning, the bin is
already at maximum frustration and will never obey — which kills beat 3,
where the audience needs to see it *sometimes* comply so they understand it is
choosing not to.

---

## 2. The script

### Beat 1 — It notices you (0:00–0:25)

**Walk toward the bin. Say nothing.**

The eye turns to follow you. The LEDs pulse. The display reads `OH NO.`

Let that sit for two seconds before you do anything. The whole premise lands
here, silently, and every second you do not speak makes it land harder.

**Throw the first item in.**

Lid opens, object goes in, green sweep, *"Acceptable."*

> First laugh. You have said nothing at all so far.

---

### Beat 2 — It has opinions (0:25–0:55)

**Throw the second item, deliberately missing.**

Lid opens. Nothing goes in. Lid closes.

Then: **nothing happens for two full seconds.** The eye is locked on you. No
sound, no lights, no movement.

Then: *"My grandmother throws better."*

> The silence is the joke. Do not fill it. Do not look at the audience. Look
> at the bin, the way you would look at a colleague who has just said
> something rude.

**Throw a third, and miss again.**

*"Again?"* — the escalation is visible, and the audience now understands the
machine is keeping score.

---

### Beat 3 — The remote (0:55–1:30)

**Pick up the remote. Show it to the room.** It is absurdly large, it has an
antenna, and it says **PLEASE DO NOT TRUST.**

Press **OPEN**. → The lid closes.

Look at the remote. Press **CLOSE**. → The lid opens.

> Second big laugh. Still no explanation from you.

Press **STOP**. → Everything speeds up. The display says `FASTER`.

Press **AI**. → `CALIBRATING USER…` climbs, stalls at 99 %, and delivers
`RESULT: YOU SHOULD THROW BETTER.` Then it does something completely
unrelated.

Put the remote down with visible disappointment.

---

### Beat 4 — The punchline (1:30–2:00)

**Point at the big illuminated button. Say the only line you need:**

> "There's a normal mode."

**Press it.**

The lights go clean white. The display says `NORMAL MODE ACTIVATED`. Throw
something in — the lid opens and closes properly, politely, like an ordinary
automatic bin.

**Now stop talking and take one step back.**

- The eye turns, slowly, to look at the button.
- Nothing happens for a moment.
- A hatch opens in the front panel.
- **Nothing happens for another moment.**
- An enormous finger extends, flips the switch off, and retracts.
- The hatch closes.
- Display: **`NORMAL MODE CANCELLED`**

> That is the end of the demo. Do not add anything. Let them react, then stop.

---

## 3. Timing notes

The two pauses — the 900 ms before the hatch opens, and the 900 ms before the
finger comes out — are the most important numbers in the firmware
(`FINGER_PAUSE_MS`). They are what turn a mechanism into a joke. If the
punchline is not landing in rehearsal, lengthen them before you change
anything else.

`NORMAL_MODE_GRACE_MS` (4.5 s) is the second-most important. Long enough that
the audience believes the bin has genuinely reformed; short enough that they
have not lost interest.

---

## 4. What to say, and what not to

**Say:**
- "There's a normal mode." (one line, beat 4)
- Nothing else, unless asked.

**Do not say:**
- "So what's happening here is…"
- "It's using a time-of-flight sensor to…"
- "Wait, it normally works…"

Save the engineering for questions. When they come, you have:

- The torque budget and why the lid cannot slam (`CAD_README.md` §1)
- Two independent hand-detection channels, OR-ed (`FIRMWARE_README.md` §4)
- The deterministic mistranslation engine — *"it's not random, it's
  deliberate, here are the probability tables"* (`REMOTE_PROTOCOL.md` §5)
- ESP-NOW, so it works with the venue Wi-Fi switched off

The contrast between the seriousness of those answers and the stupidity of the
machine is itself the pitch.

---

## 5. Failure-proofing

### The hidden trigger

A concealed button on GPIO13 fires a **complete, indistinguishable success
reaction**: lid closes, green sweep, approving line, counters incremented.

Use it when a throw misses and you needed it to land. Nobody can tell.

**Rules:**
- Mount it where your hand naturally rests. Do not reach for it obviously.
- Press it *while the object is still in the air*, not after it has bounced
  off the rim.
- It also clears a `LID ERROR` — see `TROUBLESHOOTING.md` §6.
- Never mention it. Not in the demo, not in the Q&A, not in the README you
  show the judges. (It is documented here because your own team needs to know.)

### If something breaks mid-demo

The machine is designed so no single failure kills the show:

| Broken | What you do |
|---|---|
| Lid | Skip to the remote and the punchline. Both still work. |
| Sensors | Hidden trigger. Nothing visibly changes. |
| Remote | The bin's own behaviour and the NORMAL MODE punchline carry it. |
| Audio | Everything is also on the display. Turn and read it aloud, deadpan. |
| Finger | This is the one that hurts. Swap the spare cartridge (four screws) if you have time; otherwise end on the remote and describe the finger. |
| Everything | *"It has decided not to cooperate today."* That is genuinely on-brand, and the honest fallback beats visible panic. |

Keep on the table: a spare finger cartridge, a spare USB-C cable, a laptop
that can reflash, and spare paper balls.

---

## 6. Handling the obvious questions

**"Is it actually doing AI?"**
No, and the firmware never claims otherwise anywhere a user can see. `AI MODE`
is a progress bar with opinions. If we had put a model on it we would have
documented the model and the inference pipeline. Answer this one honestly and
immediately — a project that fakes an AI claim loses the room, and *"no, it's
a progress bar, that's the joke"* gets a laugh.

**"Is it safe?"**
Yes, and here is the test log. Two independent sensors watch the closing zone,
either one stops and reverses the lid, the motion is cosine-eased so it cannot
slam, hard mechanical stops limit travel, and the servo rail defaults to off
at reset. `TEST_PLAN.md` §2 is 13 tests and they all pass.

**"How long did this take?"**
Longer than it should have. That is the point.

**"Why?"**
Because we could build it.

---

## 7. Sixty-second version

If you get cut short, drop beats 1 and 2 entirely:

1. Throw something in. It works. (10 s)
2. Remote: OPEN closes, CLOSE opens. (20 s)
3. "There's a normal mode." Press it. Wait. Finger. (30 s)

The punchline is the demo. Everything before it is setup, and setup is what
you cut.
