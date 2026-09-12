#!/usr/bin/env python3
"""
render_docs.py - regenerate every diagram in docs/ from source.

    python tools/render_docs.py

The diagrams are drawn in code rather than hand-edited in a drawing program
so that when the pin map or the state machine changes, the picture can be
updated in the same commit as the thing it describes.

Requires: matplotlib (no other dependencies).
"""

import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Circle, Rectangle, Polygon

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs")
os.makedirs(OUT, exist_ok=True)

INK      = "#1b1b1f"
MUTED    = "#6b6b76"
ACCENT   = "#c8442c"      # actuators / danger
SENSOR   = "#2f6f8f"      # sensors
UI       = "#7a5ea8"      # user interface
POWER    = "#b8860b"      # power
PAPER    = "#fbfaf7"
LINE     = "#3a3a42"


def box(ax, x, y, w, h, label, color=INK, fc="white", fontsize=9, bold=False, r=0.06):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h,
        boxstyle=f"round,pad=0.02,rounding_size={r}",
        linewidth=1.4, edgecolor=color, facecolor=fc, zorder=2))
    ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
            fontsize=fontsize, color=INK, zorder=3,
            fontweight="bold" if bold else "normal", linespacing=1.35)


def arrow(ax, p1, p2, color=LINE, style="-|>", lw=1.3, rad=0.0, ls="-"):
    ax.add_patch(FancyArrowPatch(
        p1, p2, arrowstyle=style, mutation_scale=12,
        linewidth=lw, color=color, zorder=1, linestyle=ls,
        connectionstyle=f"arc3,rad={rad}",
        shrinkA=3, shrinkB=3))


def canvas(w, h, title=None, sub=None):
    fig, ax = plt.subplots(figsize=(w, h), dpi=150)
    ax.set_facecolor(PAPER)
    fig.patch.set_facecolor(PAPER)
    ax.set_xticks([]); ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    if title:
        ax.text(0.5, 0.975, title, transform=ax.transAxes, ha="center",
                va="top", fontsize=13, fontweight="bold", color=INK)
    if sub:
        ax.text(0.5, 0.936, sub, transform=ax.transAxes, ha="center",
                va="top", fontsize=8.5, color=MUTED)
    return fig, ax


def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)
    print("wrote", os.path.relpath(path))


# ===========================================================================
def architecture():
    fig, ax = canvas(11, 7.6, "BIN-CHAD system architecture",
                     "ESP32-S3 main controller - ESP-NOW link to a dedicated remote - no cloud, no router, no app")
    ax.set_xlim(0, 11); ax.set_ylim(0, 7.6)

    box(ax, 4.1, 5.5, 2.8, 1.0, "ESP32-S3\nmain controller", INK, "#eceaf5", 10, True)

    sensors = [
        ("VL53L0X  throat\nobject + hand safety", 0.25, 3.8),
        ("VL53L0X  approach\nperson detect", 0.25, 2.9),
        ("IR break-beam\nsafety backup", 0.25, 2.0),
        ("Lid limit switches\nopen / closed", 0.25, 1.1),
        ("NORMAL switch\n+ hidden trigger", 0.25, 0.2),
    ]
    for label, x, y in sensors:
        box(ax, x, y, 2.5, 0.72, label, SENSOR, "#eaf1f5", 8)
        arrow(ax, (2.75, y + 0.36), (4.35, 5.5), SENSOR, rad=0.12)

    actuators = [
        ("Lid servo  MG996R\neased, obstruction-aware", 8.25, 3.8),
        ("Eye servos  pan/tilt", 8.25, 2.9),
        ("Finger servo", 8.25, 2.0),
        ("Hatch servo", 8.25, 1.1),
    ]
    for label, x, y in actuators:
        box(ax, x, y, 2.5, 0.72, label, ACCENT, "#f7eceb", 8)
        arrow(ax, (6.65, 5.5), (8.25, y + 0.36), ACCENT, rad=-0.12)

    ui = [
        ("SSD1306 OLED\nface + status", 3.15, 3.9),
        ("WS2812B x12", 3.15, 3.0),
        ("MAX98357A\n+ 4R speaker", 3.15, 2.1),
    ]
    for label, x, y in ui:
        box(ax, x, y, 2.2, 0.72, label, UI, "#f1ecf7", 8)
        arrow(ax, (5.5, 5.5), (x + 1.1, y + 0.72), UI, rad=0.0)

    box(ax, 4.1, 0.15, 2.8, 0.85, "BIN CONTROL SYSTEM\nESP32-C3 remote", MUTED, "white", 9, True)
    arrow(ax, (5.5, 1.0), (5.5, 1.95), MUTED, style="<|-|>", lw=1.6)
    ax.text(5.62, 1.48, "ESP-NOW\nch.1, 8-byte packets", fontsize=7.5, color=MUTED, va="center")

    box(ax, 7.9, 6.0, 2.9, 0.9,
        "5 V / 5 A supply\nservo rail + logic rail\nshared ground, fused",
        POWER, "#f7f2e3", 8)
    arrow(ax, (7.9, 6.45), (6.9, 6.2), POWER, style="-|>")

    ax.text(0.25, 6.9, "SENSE", fontsize=9, fontweight="bold", color=SENSOR)
    ax.text(8.25, 4.75, "ACT", fontsize=9, fontweight="bold", color=ACCENT)
    ax.text(3.15, 4.75, "EXPRESS", fontsize=9, fontweight="bold", color=UI)
    save(fig, "architecture.png")


# ===========================================================================
def wiring():
    fig, ax = canvas(11, 7.2, "Power architecture and wiring topology",
                     "Servos never draw through the ESP32. One star ground. One switch that kills all actuator power.")
    ax.set_xlim(0, 11); ax.set_ylim(0, 7.2)

    box(ax, 0.3, 5.9, 2.3, 0.9, "MAINS PSU\n5 V  5 A\n(25 W)", POWER, "#f7f2e3", 9, True)
    box(ax, 3.1, 5.9, 1.6, 0.9, "MASTER\nSWITCH", INK, "white", 9, True)
    box(ax, 5.2, 5.9, 1.7, 0.9, "FUSE\n5 A\nblade", ACCENT, "#f7eceb", 9, True)
    arrow(ax, (2.6, 6.35), (3.1, 6.35))
    arrow(ax, (4.7, 6.35), (5.2, 6.35))

    # distribution bar
    ax.plot([6.9, 9.9], [6.35, 6.35], color=POWER, lw=3, zorder=1)
    ax.plot([9.9, 9.9], [6.35, 1.2], color=POWER, lw=3, zorder=1)
    ax.text(8.2, 6.55, "5 V distribution bar", fontsize=8, color=POWER, ha="center")

    rails = [
        ("Servo rail\n5 V, 4 A budget\n1000 uF bulk cap", 6.6, 4.6, ACCENT, "#f7eceb"),
        ("LED rail\n5 V, 0.8 A\n470 uF + 330R data", 6.6, 3.5, UI, "#f1ecf7"),
        ("Audio  MAX98357A\n5 V, 0.6 A peak", 6.6, 2.4, UI, "#f1ecf7"),
        ("Logic  buck 5->3V3\nor ESP32 5 V pin", 6.6, 1.3, SENSOR, "#eaf1f5"),
    ]
    for label, x, y, c, fc in rails:
        box(ax, x, y, 3.0, 0.82, label, c, fc, 8)
        arrow(ax, (9.9, y + 0.41), (9.6, y + 0.41), POWER)

    box(ax, 0.3, 3.3, 2.6, 1.6,
        "ESP32-S3\n\nGPIO out to servo\nsignal pins only.\nNo servo current\nthrough the board.",
        INK, "white", 8)

    box(ax, 0.3, 1.4, 2.6, 1.3,
        "P-FET / relay on\nPIN_SERVO_POWER_EN\n\nfirmware E-stop:\ncuts the servo rail",
        ACCENT, "#f7eceb", 8)
    arrow(ax, (1.6, 3.3), (1.6, 2.7), ACCENT)
    arrow(ax, (2.9, 2.05), (6.6, 4.7), ACCENT, rad=-0.2, ls="--")

    # ground star
    ax.add_patch(Circle((5.0, 0.6), 0.3, facecolor="white", edgecolor=INK, lw=1.6, zorder=3))
    ax.text(5.0, 0.6, "GND", ha="center", va="center", fontsize=8, fontweight="bold", zorder=4)
    for x in (1.6, 3.2, 6.6, 8.1, 9.6):
        arrow(ax, (x, 1.0), (5.0, 0.85), MUTED, style="-", lw=1.0, rad=0.1)
    ax.text(5.0, 0.12, "single star ground - servo return never shares a trace with sensor return",
            ha="center", fontsize=7.5, color=MUTED)
    save(fig, "wiring.png")


# ===========================================================================
def state_machine():
    fig, ax = canvas(12, 8.0, "BIN-CHAD main state machine",
                     "Every transition is millis()-driven. Nothing blocks. Safety transitions (red) preempt all others.")
    ax.set_xlim(0, 12); ax.set_ylim(0, 8)

    nodes = {
        "BOOT":        (0.5, 6.35, INK,    "white"),
        "SELF_TEST":   (2.4, 6.35, INK,    "white"),
        "IDLE":        (5.0, 6.35, INK,    "#eceaf5"),
        "SLEEP":       (8.6, 6.35, MUTED,  "white"),
        "CURIOUS":     (5.0, 5.05, SENSOR, "#eaf1f5"),
        "THROW_WAIT":  (5.0, 3.75, SENSOR, "#eaf1f5"),
        "SUCCESS":     (2.6, 2.55, "#2e7d4f", "#e8f3ec"),
        "MISS_SILENCE":(5.0, 2.55, ACCENT, "#f7eceb"),
        "MISS_REACT":  (5.0, 1.35, ACCENT, "#f7eceb"),
        "REMOTE_RESP": (8.6, 4.85, UI,     "#f1ecf7"),
        "AI_THEATRE":  (10.3, 3.65, UI,    "#f1ecf7"),
        "NORMAL_MODE": (8.6, 2.55, "#2e7d4f", "#e8f3ec"),
        "SELF_DISABLE":(8.6, 1.35, ACCENT, "#f7eceb"),
        "APOLOGY":     (2.6, 1.35, ACCENT, "#f7eceb"),
        "FAULT":       (0.7, 3.75, ACCENT, "#f7eceb"),
    }
    W, H = 1.6, 0.62
    for name, (x, y, c, fc) in nodes.items():
        box(ax, x, y, W, H, name.replace("_", "\n") if len(name) > 11 else name,
            c, fc, 8, name in ("IDLE", "SELF_DISABLE"))

    def c(n, side="r"):
        x, y, _, _ = nodes[n]
        return {"r": (x + W, y + H / 2), "l": (x, y + H / 2),
                "t": (x + W / 2, y + H), "b": (x + W / 2, y)}[side]

    edges = [
        # from, side, to, side, colour, label, label dy
        ("BOOT", "r", "SELF_TEST", "l", LINE, "", 0),
        ("SELF_TEST", "r", "IDLE", "l", LINE, "", 0),
        ("IDLE", "r", "SLEEP", "l", MUTED, "90 s idle", 0.30),
        ("SLEEP", "b", "IDLE", "b", MUTED, "person near", -0.42),
        ("IDLE", "b", "CURIOUS", "t", SENSOR, "approach", 0.02),
        ("CURIOUS", "b", "THROW_WAIT", "t", SENSOR, "object", 0.02),
        ("THROW_WAIT", "l", "SUCCESS", "r", "#2e7d4f", "passed through", 0.22),
        ("THROW_WAIT", "b", "MISS_SILENCE", "t", ACCENT, "2.6 s timeout", 0.02),
        ("MISS_SILENCE", "b", "MISS_REACT", "t", ACCENT, "2 s silence", 0.02),
        ("SUCCESS", "t", "IDLE", "l", LINE, "", 0),
        ("MISS_REACT", "l", "IDLE", "b", LINE, "", 0),
        ("IDLE", "r", "REMOTE_RESP", "l", UI, "remote cmd", -0.34),
        ("REMOTE_RESP", "r", "AI_THEATRE", "t", UI, "AI", 0.16),
        ("AI_THEATRE", "l", "REMOTE_RESP", "b", UI, "", 0),
        ("REMOTE_RESP", "b", "NORMAL_MODE", "t", MUTED, "", 0),
        ("NORMAL_MODE", "b", "SELF_DISABLE", "t", ACCENT, "grace expires", 0.02),
        ("SELF_DISABLE", "l", "IDLE", "b", LINE, "finger retracts", 0.30),
    ]
    for a, sa, b, sb, col, lab, ldy in edges:
        p1, p2 = c(a, sa), c(b, sb)
        arrow(ax, p1, p2, col, rad=0.15)
        if lab:
            ax.text((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2 + 0.16 + ldy, lab,
                    fontsize=6.8, color=col, ha="center",
                    bbox=dict(boxstyle="round,pad=0.14", fc=PAPER, ec="none"))

    # Safety / fault transitions
    arrow(ax, c("THROW_WAIT", "l"), c("APOLOGY", "r"), ACCENT, lw=2.0, rad=0.3)
    ax.text(3.6, 2.55, "hand detected\nlid reverses", fontsize=6.8, color=ACCENT, ha="center")
    arrow(ax, c("APOLOGY", "t"), c("IDLE", "l"), LINE, rad=0.25)
    arrow(ax, c("SELF_TEST", "l"), c("FAULT", "t"), ACCENT, rad=0.2)
    ax.text(1.1, 5.6, "limit switch\ndisagrees", fontsize=6.8, color=ACCENT, ha="center")

    ax.text(0.4, 0.35, "NORMAL MODE switch is polled from every state (handleNormalSwitch)\n"
                       "Obstruction check runs in Lid::update() before any motion step, on every loop pass",
            fontsize=7.5, color=MUTED)
    save(fig, "state-machine.png")


# ===========================================================================
def mechanism():
    fig, ax = canvas(11, 6.4, "Lid linkage and the useless-finger mechanism",
                     "Left: lid geometry and the torque sum that picks the servo.  Right: the self-disable cartridge.")
    ax.set_xlim(0, 11); ax.set_ylim(0, 6.4)

    # ---- LID, side elevation -------------------------------------------
    ax.add_patch(Rectangle((0.6, 0.8), 4.4, 1.9, facecolor="#efeef4",
                           edgecolor=INK, lw=1.5, zorder=1))
    ax.text(2.8, 1.2, "bin body", fontsize=8, color=MUTED, ha="center")

    hinge = (4.8, 2.7)
    ax.add_patch(Circle(hinge, 0.11, facecolor="white", edgecolor=INK, lw=1.6, zorder=4))
    ax.text(5.05, 2.72, "hinge", fontsize=7.5, color=INK)

    # closed lid
    ax.add_patch(Rectangle((0.6, 2.7), 4.2, 0.16, facecolor=ACCENT,
                           edgecolor=INK, lw=1.2, zorder=3))
    # open lid, rotated about the hinge
    ax.add_patch(Polygon([(4.8, 2.7), (2.05, 5.85), (2.16, 5.96), (4.85, 2.84)],
                         closed=True, facecolor="#f0c3bb", edgecolor=INK,
                         lw=1.2, zorder=2))
    ax.annotate("", xy=(3.1, 4.55), xytext=(3.9, 3.05),
                arrowprops=dict(arrowstyle="->", color=ACCENT, lw=1.4,
                                connectionstyle="arc3,rad=0.35"))
    ax.text(3.55, 3.95, "84 deg", fontsize=7.5, color=ACCENT)

    # centre of gravity
    ax.plot([2.7], [2.78], marker="o", color=INK, markersize=5, zorder=5)
    ax.annotate("", xy=(2.7, 2.78), xytext=(4.8, 2.78),
                arrowprops=dict(arrowstyle="<->", color=MUTED, lw=1.0))
    ax.text(3.65, 2.94, "d = 110 mm", fontsize=7.5, color=MUTED, ha="center")
    ax.annotate("", xy=(2.7, 2.2), xytext=(2.7, 2.75),
                arrowprops=dict(arrowstyle="->", color=INK, lw=1.2))
    ax.text(2.78, 2.3, "m·g", fontsize=7.5, color=INK)

    # servo + linkage
    ax.add_patch(Rectangle((1.5, 1.15), 0.85, 0.62, facecolor="white",
                           edgecolor=ACCENT, lw=1.4, zorder=3))
    ax.text(1.93, 1.46, "servo", fontsize=7, color=ACCENT, ha="center", zorder=4)
    ax.plot([2.35, 2.9], [1.46, 1.95], color=INK, lw=2.0, zorder=3)   # horn
    ax.plot([2.9, 3.6], [1.95, 2.7], color=SENSOR, lw=2.4, zorder=3)  # link
    ax.text(3.15, 2.25, "link\n62 mm", fontsize=7, color=SENSOR)

    ax.text(0.55, 0.25,
            "tau_static = m·g·d = 0.180 × 9.81 × 0.110 = 0.194 N·m = 1.98 kgf·cm\n"
            "required (×2 safety) = 3.96 kgf·cm      fitted MG996R @ 5 V = 9.4 kgf·cm  ->  4.7× margin",
            fontsize=7.6, color=INK, family="monospace")

    # ---- FINGER MODULE -------------------------------------------------
    bx, by = 6.4, 1.5
    ax.add_patch(Rectangle((bx, by), 2.6, 3.1, facecolor="white",
                           edgecolor=INK, lw=1.6, zorder=2))
    ax.text(bx + 1.3, by + 3.3, "finger cartridge", fontsize=8.5,
            fontweight="bold", ha="center")

    # hatch
    ax.add_patch(Rectangle((bx + 2.55, by + 2.0), 0.1, 0.9,
                           facecolor=ACCENT, edgecolor=INK, lw=1.2, zorder=3))
    ax.add_patch(Polygon([(bx + 2.6, by + 2.9), (bx + 3.35, by + 3.35),
                          (bx + 3.42, by + 3.22), (bx + 2.68, by + 2.82)],
                         closed=True, facecolor="#f0c3bb", edgecolor=INK, lw=1.0))
    ax.text(bx + 3.05, by + 3.45, "hatch", fontsize=7, color=MUTED)

    # finger, extended
    ax.plot([bx + 0.9, bx + 3.55], [by + 2.45, by + 2.45], color=ACCENT, lw=6,
            solid_capstyle="round", zorder=4)
    ax.add_patch(Circle((bx + 0.9, by + 2.45), 0.16, facecolor="white",
                        edgecolor=INK, lw=1.4, zorder=5))
    ax.text(bx + 0.55, by + 2.05, "extend\nservo", fontsize=7, color=MUTED, ha="center")
    ax.add_patch(Circle((bx + 3.62, by + 2.45), 0.13, facecolor="#f0c3bb",
                        edgecolor=INK, lw=1.2, zorder=5))
    ax.text(bx + 3.66, by + 2.14, "TPU tip", fontsize=6.8, color=MUTED)

    # the switch
    ax.add_patch(Rectangle((bx + 3.75, by + 2.05), 0.42, 0.8,
                           facecolor="#c33", edgecolor=INK, lw=1.4, zorder=3))
    ax.text(bx + 3.96, by + 3.0, "NORMAL\nMODE", fontsize=7, ha="center",
            color=INK, fontweight="bold")

    ax.text(bx - 0.05, by - 1.0,
            "sequence:  hatch opens -> 900 ms pause -> finger extends -> 260 ms dwell\n"
            "           -> switch verified OFF (retry once) -> retract -> hatch shuts",
            fontsize=7.4, color=INK, family="monospace")
    save(fig, "mechanism.png")


# ===========================================================================
def exploded():
    fig, ax = canvas(9.5, 8.2, "Assembly stack-up (exploded)",
                     "Every module is removable on M3 screws into heat-set inserts. Nothing is glued.")
    ax.set_xlim(0, 9.5); ax.set_ylim(0, 8.2)

    layers = [
        ("LID  ribbed frame + hinge brackets",      6.95, ACCENT, "#f7eceb"),
        ("COLLAR  hinge mounts, lid stops, LED ring", 5.95, UI,   "#f1ecf7"),
        ("FACE PANEL  OLED bezel, eye gimbal, ToF",  4.95, SENSOR,"#eaf1f5"),
        ("FINGER CARTRIDGE + NORMAL switch plate",   3.95, ACCENT,"#f7eceb"),
        ("ELECTRONICS TRAY  ESP32-S3, buck, amp",    2.95, INK,   "white"),
        ("BIN BODY  waste liner, service panel",     1.95, MUTED, "white"),
        ("BASE  4x TPU feet, PSU inlet, master switch", 0.95, POWER, "#f7f2e3"),
    ]
    for label, y, c, fc in layers:
        box(ax, 1.4, y, 6.7, 0.72, label, c, fc, 9)
        if y > 1.0:
            arrow(ax, (0.95, y), (0.95, y - 0.28), MUTED, style="-|>", lw=1.0)

    ax.text(0.35, 4.0, "assembly order", rotation=90, fontsize=8,
            color=MUTED, va="center")

    ax.text(8.35, 6.6, "M3 x 12\ninserts", fontsize=7, color=MUTED, ha="center")
    ax.text(8.35, 3.3, "M3 x 8\ninserts", fontsize=7, color=MUTED, ha="center")

    ax.text(1.4, 0.35,
            "Service access: rear panel off -> tray slides out with loom attached.\n"
            "Lid lifts off by pulling one 4 mm hinge pin. Finger cartridge is 4 screws.",
            fontsize=7.8, color=INK)
    save(fig, "assembly-exploded.png")


if __name__ == "__main__":
    architecture()
    wiring()
    state_machine()
    mechanism()
    exploded()
    print("done")
