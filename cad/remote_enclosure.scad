// =============================================================================
//  remote_enclosure.scad - BIN CONTROL SYSTEM v0.0001
//
//    part = "shell"      x1 - main body, print open-face-up
//    part = "faceplate"  x1 - top panel with all the button holes
//    part = "cap_round"  x7 - caps for the D-pad / OPEN / CLOSE buttons
//    part = "cap_danger" x1 - the big red one that does nothing useful
//    part = "antenna"    x1 - purely decorative. Non-negotiable.
//    part = "battery_door" x1
//
//  Design intent: this should look like it was requisitioned from a nuclear
//  facility in 1978 and should be too big for one hand. If a judge picks it
//  up and laughs before pressing anything, the industrial design has done
//  its job.
//
//  The button layout matches the firmware's pin map:
//      row 1:            UP
//      row 2:   LEFT     OK      RIGHT
//      row 3:            DOWN
//      row 4:   OPEN             CLOSE
//      aux strip (ladder): AI ANGRY MOOD STOP NORMAL MUTE LIGHT DARK
// =============================================================================

include <binchad_params.scad>

part = "all";

// Button centres on the faceplate, in mm from the plate centre.
dpad_pitch = 30;
dpad_y     = 14;

btn_positions = [
    [ 0,             dpad_y + dpad_pitch ],   // UP
    [-dpad_pitch,    dpad_y              ],   // LEFT
    [ 0,             dpad_y              ],   // OK
    [ dpad_pitch,    dpad_y              ],   // RIGHT
    [ 0,             dpad_y - dpad_pitch ],   // DOWN
    [-46,            dpad_y - dpad_pitch ],   // OPEN
    [ 46,            dpad_y - dpad_pitch ]    // CLOSE
];

// Aux ladder buttons: 8 x 12 mm tactile, in a row along the bottom.
aux_count = 8;
aux_pitch = 20;
aux_y     = -38;

// ---------------------------------------------------------------------------
module remote_shell() {
    difference() {
        rbox(remote_x, remote_y, remote_z, 8);
        // hollow
        translate([0, 0, remote_wall])
            rbox(remote_x - remote_wall*2, remote_y - remote_wall*2,
                 remote_z, 6);
        // faceplate rebate
        translate([0, 0, remote_z - 3.2])
            rbox(remote_x - remote_wall*2 + fit_clear,
                 remote_y - remote_wall*2 + fit_clear, 3.4, 6);
        // antenna hole, top right
        translate([remote_x/2 - 18, remote_y/2 - 14, remote_z - 6])
            cylinder(d = remote_antenna_d + fit_snug, h = 8);
        // USB-C service slot
        translate([-remote_x/2 - 0.1, -12, 8]) cube([remote_wall + 0.2, 14, 9]);
        // battery door aperture in the base
        translate([0, -remote_y/2 + 34, -0.1]) rbox(64, 34, remote_wall + 0.2, 3);
    }

    // faceplate insert bosses
    for (dx = [-1, 1], dy = [-1, 1])
        translate([dx * (remote_x/2 - 10), dy * (remote_y/2 - 10), remote_wall])
            insert_boss(remote_z - remote_wall - 3.4);

    // antenna boss, internal
    translate([remote_x/2 - 18, remote_y/2 - 14, remote_wall])
        difference() {
            cylinder(d = remote_antenna_d + 6, h = 10);
            translate([0, 0, -0.1]) cylinder(d = insert_m3_d, h = insert_m3_h);
        }
}

// ---------------------------------------------------------------------------
module remote_faceplate() {
    difference() {
        rbox(remote_x - remote_wall*2, remote_y - remote_wall*2, 3, 6);

        // main buttons
        for (p = btn_positions)
            translate([p[0], p[1], -0.1])
                cylinder(d = remote_btn_d + fit_clear, h = 3.2);

        // aux ladder row
        for (i = [0 : aux_count - 1])
            translate([(i - (aux_count - 1)/2) * aux_pitch, aux_y, -0.1])
                cylinder(d = 12.4, h = 3.2);

        // OLED window
        translate([-52, remote_y/2 - 30, -0.1])
            rbox(oled_win_x + 2, oled_win_y + 2, 3.2, 1);

        // fake "AI" indicator - a 5 mm LED that means nothing
        translate([56, remote_y/2 - 30, -0.1]) cylinder(d = 5.2, h = 3.2);

        // fixing screws
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * (remote_x/2 - 10), dy * (remote_y/2 - 10), -0.1])
                m3_cbore(3.2, 2.0);

        // Recessed label panels. Apply printed labels or a second-colour
        // inlay: "BIN CONTROL SYSTEM", "v0.0001", "PLEASE DO NOT TRUST."
        translate([0, remote_y/2 - 14, 3 - 0.6]) rbox(96, 12, 0.8, 2);
        translate([0, -remote_y/2 + 12, 3 - 0.6]) rbox(120, 10, 0.8, 2);

        // Hazard stripe recess down each side, for a contrasting inlay.
        for (dx = [-1, 1])
            translate([dx * (remote_x/2 - 14), 0, 3 - 0.6])
                rbox(10, remote_y - 30, 0.8, 2);
    }
}

// ---------------------------------------------------------------------------
module cap_round(d = remote_btn_d, h = 8) {
    difference() {
        union() {
            cylinder(d = d, h = h - 2);
            translate([0, 0, h - 2]) scale([1, 1, 0.4]) sphere(d = d);
        }
        // socket for a 12 mm tactile switch plunger
        translate([0, 0, -0.1]) cylinder(d = 4.2, h = 4);
        translate([0, 0, -0.1]) cylinder(d = d - 3, h = 2.2);
    }
}

module cap_danger() {        // print red. Obviously.
    difference() {
        union() {
            cylinder(d1 = 30, d2 = 26, h = 9);
            translate([0, 0, 9]) scale([1, 1, 0.45]) sphere(d = 26);
        }
        translate([0, 0, -0.1]) cylinder(d = 4.2, h = 5);
        translate([0, 0, -0.1]) cylinder(d = 22, h = 2.6);
    }
}

// ---------------------------------------------------------------------------
// ANTENNA
// Electrically inert. The ESP32-C3's actual antenna is a trace on the
// module. This exists to be seen.
// ---------------------------------------------------------------------------
module antenna() {
    union() {
        // threaded-look base
        for (i = [0 : 5])
            translate([0, 0, i * 2.2])
                cylinder(d1 = remote_antenna_d + 3, d2 = remote_antenna_d + 1, h = 2.2);
        translate([0, 0, 14])
            cylinder(d1 = remote_antenna_d, d2 = remote_antenna_d * 0.55,
                     h = remote_antenna_h - 20);
        translate([0, 0, remote_antenna_h - 6]) sphere(d = 8);
        // mounting spigot
        translate([0, 0, -8]) cylinder(d = insert_m3_d - 0.2, h = 8);
    }
}

module battery_door() {
    difference() {
        union() {
            rbox(64 - fit_clear, 34 - fit_clear, remote_wall, 3);
            translate([0, 0, remote_wall]) rbox(58, 28, 1.6, 2);
        }
        for (dx = [-1, 1])
            translate([dx * 24, 0, -0.1]) m3_cbore(remote_wall + 0.2, 1.6);
    }
}

// ---------------------------------------------------------------------------
if (part == "shell")             remote_shell();
else if (part == "faceplate")    remote_faceplate();
else if (part == "cap_round")    cap_round();
else if (part == "cap_danger")   cap_danger();
else if (part == "antenna")      antenna();
else if (part == "battery_door") battery_door();
else {
    translate([   0,    0, 0]) remote_shell();
    translate([   0,  140, 0]) remote_faceplate();
    translate([-110,    0, 0]) cap_round();
    translate([-110,   30, 0]) cap_danger();
    translate([-110,  -60, 0]) antenna();
    translate([ 110,    0, 0]) battery_door();
}
