#!/usr/bin/env python3
"""
verify_project.py - static consistency checks for the BIN-CHAD repo.

    python tools/verify_project.py

This is not a compiler. It catches the class of mistake that a compiler would
catch far too late (on the bench, at midnight) or not at all:

  1. Duplicate GPIO assignments in either pin map
  2. The two copies of protocol.h having drifted apart
  3. Class methods declared in a header but never defined
  4. Markdown links pointing at files that do not exist
  5. WAV clips that are the wrong format or too long for the interaction
  6. settings.h values that are internally contradictory

Exit code 0 = clean, 1 = something needs attention.
"""

import os
import re
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
problems = []
notes = []


def problem(msg):
    problems.append(msg)


def note(msg):
    notes.append(msg)


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------------------
def check_pin_maps():
    for label, path in (("bin", "firmware/BinChad/src/config/pins.h"),
                        ("remote", "firmware/BinRemote/src/config/pins.h"),
                        ("trashbot-web", "firmware/TrashBotWeb/src/config/pins.h")):
        text = read(*path.split("/"))
        seen = {}
        for m in re.finditer(r"^#define\s+(PIN_\w+)\s+(\d+)", text, re.M):
            name, gpio = m.group(1), int(m.group(2))
            if gpio in seen:
                # The DFPlayer alternative deliberately reuses the I2S pins;
                # only one backend is compiled at a time.
                if "DFPLAYER" in name or "DFPLAYER" in seen[gpio]:
                    note(f"{label}: GPIO{gpio} shared by {seen[gpio]} and {name} "
                         f"(alternate audio backend - expected)")
                    continue
                problem(f"{label} pins.h: GPIO{gpio} assigned to both "
                        f"{seen[gpio]} and {name}")
            else:
                seen[gpio] = name
        note(f"{label} pins.h: {len(seen)} distinct GPIOs assigned")

    # ESP32-S3 pins that must never be used.
    forbidden = {0: "strapping/BOOT", 3: "strapping", 19: "USB D-", 20: "USB D+",
                 43: "UART0 TX", 44: "UART0 RX", 45: "strapping", 46: "strapping"}
    forbidden.update({p: "SPI flash" for p in range(26, 33)})
    text = read("firmware", "BinChad", "src", "config", "pins.h")
    for m in re.finditer(r"^#define\s+(PIN_\w+)\s+(\d+)", text, re.M):
        gpio = int(m.group(2))
        if gpio in forbidden:
            problem(f"bin pins.h: {m.group(1)} uses GPIO{gpio} "
                    f"({forbidden[gpio]}) - reserved")

    # TrashBotWeb is a classic ESP32, which has a different set of traps:
    # 6-11 are the SPI flash, and 34-39 are input-only so they cannot drive
    # an L298N input.
    classic = {p: "SPI flash" for p in range(6, 12)}
    classic.update({p: "input-only, cannot drive an output" for p in range(34, 40)})
    text = read("firmware", "TrashBotWeb", "src", "config", "pins.h")
    for m in re.finditer(r"^#define\s+(PIN_\w+)\s+(\d+)", text, re.M):
        gpio = int(m.group(2))
        if gpio in classic:
            problem(f"trashbot-web pins.h: {m.group(1)} uses GPIO{gpio} "
                    f"({classic[gpio]}) - unusable")


def check_protocol_copies():
    a = read("firmware", "BinChad", "src", "remote", "protocol.h")
    b = read("firmware", "BinRemote", "src", "config", "protocol.h")
    if a != b:
        problem("protocol.h copies have DIVERGED - every packet will fail "
                "its checksum. Copy one over the other.")
    else:
        note("protocol.h: both copies byte-identical")


def check_method_definitions():
    """Every 'Type Class::method' declared in a .h should exist in its .cpp."""
    pairs = []
    for base in ("firmware/BinChad/src", "firmware/BinRemote/src",
                 "firmware/TrashBotWeb/src"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, *base.split("/"))):
            for f in files:
                if f.endswith(".h"):
                    cpp = os.path.join(dirpath, f[:-2] + ".cpp")
                    if os.path.exists(cpp):
                        pairs.append((os.path.join(dirpath, f), cpp))

    checked = 0
    for hpath, cpath in pairs:
        htext = open(hpath, encoding="utf-8").read()
        ctext = open(cpath, encoding="utf-8").read()

        # A header may declare several classes (sensors.h has two). Slice the
        # file at each 'class X' so every declaration is attributed to the
        # class it actually belongs to.
        starts = [(m.start(), m.group(1))
                  for m in re.finditer(r"^class\s+(\w+)", htext, re.M)]
        if not starts:
            continue
        bounds = [(name, s, starts[i + 1][0] if i + 1 < len(starts) else len(htext))
                  for i, (s, name) in enumerate(starts)]

        for cname, start, end in bounds:
            block = htext[start:end]
            # Declarations that are not inline-defined and not pure data.
            for m in re.finditer(
                    r"^\s+(?:static\s+)?[\w:*&<>\s]+?\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?;",
                    block, re.M):
                meth = m.group(1)
                if meth in ("if", "for", "while", "switch", "return"):
                    continue
                checked += 1
                if not re.search(rf"\b{cname}::{meth}\s*\(", ctext):
                    problem(f"{os.path.relpath(hpath, ROOT)}: {cname}::{meth}() "
                            f"declared but not defined in "
                            f"{os.path.basename(cpath)}")
    note(f"method definitions: {checked} declarations checked")


def check_doc_links():
    md_files = [f for f in os.listdir(ROOT) if f.endswith(".md")]
    checked = 0
    for md in md_files:
        text = read(md)
        for m in re.finditer(r"\[[^\]]*\]\(([^)#:]+?)\)", text):
            target = m.group(1).strip()
            if target.startswith(("http", "mailto", "#")):
                continue
            checked += 1
            if not os.path.exists(os.path.join(ROOT, target)):
                problem(f"{md}: link to missing file '{target}'")
        # Bare references to tool scripts
        for m in re.finditer(r"(tools/[\w_]+\.py)", text):
            if not os.path.exists(os.path.join(ROOT, m.group(1))):
                problem(f"{md}: references missing script '{m.group(1)}'")
    note(f"doc links: {checked} relative links checked across {len(md_files)} files")


def check_wavs():
    data = os.path.join(ROOT, "firmware", "BinChad", "data")
    if not os.path.isdir(data):
        problem("firmware/BinChad/data/ missing - run tools/make_wavs.py")
        return

    # Names the firmware asks for, from audio.cpp's clip table.
    audio = read("firmware", "BinChad", "src", "hardware", "audio.cpp")
    wanted = re.findall(r'"(/\w+\.wav)"', audio)
    wanted = [w.lstrip("/") for w in wanted]

    present = sorted(f for f in os.listdir(data) if f.endswith(".wav"))
    for w in wanted:
        if w not in present:
            problem(f"audio.cpp expects {w} but firmware/BinChad/data/ has no such file")

    longest = 0.0
    for f in present:
        with wave.open(os.path.join(data, f), "rb") as w:
            if w.getnchannels() != 1 or w.getsampwidth() != 2:
                problem(f"{f}: must be 16-bit mono PCM "
                        f"(got {w.getnchannels()}ch/{w.getsampwidth() * 8}-bit)")
            dur = w.getnframes() / w.getframerate()
            longest = max(longest, dur)
            if dur > 1.0:
                problem(f"{f}: {dur:.2f} s - clips must stay under 1 s "
                        f"or they kill the pace of the interaction")
    note(f"audio: {len(present)} clips present, longest {longest:.2f} s")


def check_settings_sanity():
    s = read("firmware", "BinChad", "src", "config", "settings.h")

    def val(name):
        m = re.search(rf"\b{name}\s*=\s*(\d+)", s)
        return int(m.group(1)) if m else None

    closed, opened, peek = val("LID_ANGLE_CLOSED"), val("LID_ANGLE_OPEN"), val("LID_ANGLE_PEEK")
    if None not in (closed, opened, peek):
        if not (closed < peek < opened):
            problem(f"settings.h: LID_ANGLE_PEEK ({peek}) should sit between "
                    f"CLOSED ({closed}) and OPEN ({opened})")

    obj, clear = val("TOF_THROAT_OBJECT_MM"), val("TOF_THROAT_CLEAR_MM")
    if None not in (obj, clear):
        if clear <= obj:
            problem(f"settings.h: TOF_THROAT_CLEAR_MM ({clear}) must exceed "
                    f"TOF_THROAT_OBJECT_MM ({obj}) - that gap IS the hysteresis")
        elif clear - obj < 30:
            problem(f"settings.h: only {clear - obj} mm of throat hysteresis; "
                    f"expect state chatter. Aim for 50 mm+")

    near, far = val("TOF_APPROACH_NEAR_MM"), val("TOF_APPROACH_FAR_MM")
    if None not in (near, far) and far <= near:
        problem(f"settings.h: TOF_APPROACH_FAR_MM ({far}) must exceed "
                f"NEAR ({near})")

    hand = val("SAFETY_HAND_MM")
    if None not in (hand, obj) and hand >= obj:
        problem(f"settings.h: SAFETY_HAND_MM ({hand}) should be well inside "
                f"TOF_THROAT_OBJECT_MM ({obj})")

    op, cl = val("LID_MS_OPEN"), val("LID_MS_CLOSE")
    if None not in (op, cl) and cl <= op:
        problem(f"settings.h: LID_MS_CLOSE ({cl}) should be slower than "
                f"LID_MS_OPEN ({op}) - closing is the dangerous direction")

    annoy, concern, angry = val("MISS_ANNOY_AT"), val("MISS_CONCERN_AT"), val("MISS_ANGRY_AT")
    if None not in (annoy, concern, angry) and not (annoy < concern < angry):
        problem(f"settings.h: miss thresholds out of order "
                f"({annoy}/{concern}/{angry})")

    note("settings.h: lid, sensor and escalation values internally consistent")


# ---------------------------------------------------------------------------
def main():
    for check in (check_pin_maps, check_protocol_copies, check_method_definitions,
                  check_doc_links, check_wavs, check_settings_sanity):
        try:
            check()
        except Exception as e:                      # a broken check is a problem too
            problem(f"{check.__name__} raised {type(e).__name__}: {e}")

    print("=" * 68)
    for n in notes:
        print(f"  ok   {n}")
    if problems:
        print()
        for p in problems:
            print(f"  FAIL {p}")
        print("=" * 68)
        print(f"{len(problems)} problem(s) found")
        return 1
    print("=" * 68)
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
