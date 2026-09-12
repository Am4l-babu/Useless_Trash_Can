// =============================================================================
//  binchad_params.scad - shared parameters for every printed part.
//
//  Change a number here and every part that references it follows. This is
//  the whole reason the CAD is parametric: the lid width appears in the
//  hinge, the servo mount, the linkage length and the body cut-out, and
//  they must not disagree.
//
//  Units: millimetres. Printer assumptions: 0.4 mm nozzle, 0.2 mm layers.
// =============================================================================

// ---------------------------------------------------------------------------
// PRINT TOLERANCES
// Measured on a stock Ender-style printer. If your holes come out tight,
// raise fit_loose; if pins rattle, lower it. Do not scale whole parts.
// ---------------------------------------------------------------------------
fit_snug   = 0.15;   // press fit, e.g. a pin in a bracket
fit_loose  = 0.35;   // free rotation, e.g. hinge pin in its bore
fit_clear  = 0.60;   // panel in a slot, cable through a hole

$fn = 48;            // curve resolution; drop to 24 while iterating

// ---------------------------------------------------------------------------
// FASTENERS - M3 throughout, heat-set inserts everywhere a screw goes into
// plastic more than once.
// ---------------------------------------------------------------------------
m3_clear    = 3.4;    // through-hole for an M3 screw
m3_tap      = 2.9;    // self-tapping into plastic (single assembly only)
m3_head_d   = 6.2;    // socket-cap head diameter
m3_head_h   = 3.2;
m3_nut_af   = 5.6;    // across flats
m3_nut_h    = 2.6;

// Heat-set insert, M3 x 5.0 mm brass (the common "M3 short" size).
// Bore is deliberately UNDERSIZE - the insert melts its own interference.
insert_m3_d = 4.0;
insert_m3_h = 5.5;

// ---------------------------------------------------------------------------
// BIN BODY
// The reference build wraps a 210 mm square opening. If you are retrofitting
// a bought bin, measure YOUR opening and set these three numbers first.
// ---------------------------------------------------------------------------
bin_opening_x   = 210;
bin_opening_y   = 210;
bin_wall        = 3.0;    // wall thickness of the printed collar
bin_collar_h    = 26;     // how far the printed collar drops over the bin

// ---------------------------------------------------------------------------
// LID
//
//  TORQUE BUDGET - the sum that picks the servo. Redo it if you change
//  anything here; do not assume the reference servo still fits.
//
//     lid_mass_g        180 g   (printed lid + hinge hardware, weighed)
//     lid_cog_mm        110 mm  (hinge axis to centre of gravity)
//     tau_static = m*g*d = 0.180 * 9.81 * 0.110 = 0.194 N.m = 1.98 kgf.cm
//     required   = tau_static * safety_factor(2.0) = 3.96 kgf.cm
//     fitted     = MG996R at 5 V = 9.4 kgf.cm  ->  4.7x margin
//
//  The margin is generous on purpose: it covers a sticky hinge, a cold
//  servo, a sagging 5 V rail and someone resting a hand on the lid.
// ---------------------------------------------------------------------------
lid_x           = 214;
lid_y           = 214;
lid_t           = 3.0;    // top skin
lid_rib_h       = 8;      // stiffening ribs, keeps the lid light AND flat
lid_rib_t       = 2.4;
lid_mass_g      = 180;    // WEIGH YOUR LID and put the real number here
lid_cog_mm      = 110;
lid_safety_factor = 2.0;

lid_open_deg    = 84;     // mechanical travel; firmware uses 12..96 servo deg

// ---------------------------------------------------------------------------
// HINGE
// Two brackets, one 4 mm steel pin (a cut-down M4 screw or 4 mm rod).
// ---------------------------------------------------------------------------
hinge_pin_d     = 4.0;
hinge_boss_d    = 11;
hinge_bracket_t = 6;
hinge_span      = 150;    // centre-to-centre of the two brackets
hinge_offset_z  = 14;     // pin height above the collar top face

// ---------------------------------------------------------------------------
// SERVOS
// ---------------------------------------------------------------------------
// MG996R / DS3218 standard-size body
servo_std_x     = 40.5;
servo_std_y     = 20.0;
servo_std_z     = 38.0;
servo_std_tab_x = 54.0;   // across the mounting tabs
servo_std_tab_t = 2.6;
servo_std_hole_d= 4.2;    // tab holes, M4 or M3 with a washer
servo_std_shaft_off = 10.0; // tab face to output shaft centre

// SG90 / MG90S micro body
servo_micro_x     = 22.8;
servo_micro_y     = 12.4;
servo_micro_z     = 22.5;
servo_micro_tab_x = 32.2;
servo_micro_tab_t = 2.5;
servo_micro_hole_d= 2.3;  // M2 self-tapping
servo_micro_shaft_off = 5.9;

horn_screw_d    = 2.0;    // servo horn retaining screw

// ---------------------------------------------------------------------------
// LINKAGE
// Servo horn -> lid. A two-bar linkage, not a direct horn mount, so the
// servo sits low in the body and the lid gets a better lever arm.
// ---------------------------------------------------------------------------
link_len        = 62;     // hole centre to hole centre - TUNE ON ASSEMBLY
link_w          = 9;
link_t          = 4;
link_hole_d     = 3.2;    // rides on M3 shoulder screws
horn_arm_len    = 25;     // effective servo horn radius

// ---------------------------------------------------------------------------
// USELESS FINGER MODULE
// ---------------------------------------------------------------------------
finger_box_x    = 62;
finger_box_y    = 46;
finger_box_z    = 52;
finger_len      = 58;     // comically long on purpose
finger_w        = 14;
finger_t        = 6;
finger_tip_r    = 5;      // ROUNDED. No sharp edges anywhere near a user.
door_x          = 34;
door_y          = 22;
door_t          = 2.4;

// ---------------------------------------------------------------------------
// NORMAL MODE SWITCH
// A 19 mm latching pushbutton, or a 12 mm toggle. Set to match yours.
// ---------------------------------------------------------------------------
button_hole_d   = 19.2;   // 19 mm latching switch + clearance
button_plate_x  = 56;
button_plate_y  = 56;
button_plate_t  = 4;

// ---------------------------------------------------------------------------
// EYE
// ---------------------------------------------------------------------------
eye_ball_d      = 42;
eye_lens_d      = 26;     // acrylic dome or a printed clear disc
eye_pan_range   = 110;    // degrees, matches EYE_PAN_MIN..MAX in firmware

// ---------------------------------------------------------------------------
// SENSORS
// ---------------------------------------------------------------------------
vl53_pcb_x      = 25.4;   // Adafruit VL53L0X breakout
vl53_pcb_y      = 17.8;
vl53_hole_span  = 20.3;
vl53_hole_d     = 2.6;

oled_pcb_x      = 27.3;   // common 0.96" SSD1306 module
oled_pcb_y      = 27.8;
oled_win_x      = 22.0;
oled_win_y      = 11.5;
oled_hole_span_x= 23.5;
oled_hole_span_y= 23.8;

// ---------------------------------------------------------------------------
// ELECTRONICS TRAY
// ---------------------------------------------------------------------------
tray_x          = 150;
tray_y          = 110;
tray_z          = 4;
tray_wall       = 2.4;
tray_rail_w     = 6;      // slides out on rails; nothing is glued in

// ---------------------------------------------------------------------------
// REMOTE
// ---------------------------------------------------------------------------
remote_x        = 190;    // deliberately, absurdly oversized
remote_y        = 110;
remote_z        = 34;
remote_wall     = 2.4;
remote_btn_d    = 16;     // arcade-style 16 mm buttons
remote_antenna_d= 8;
remote_antenna_h= 120;    // does nothing. Essential.

// ---------------------------------------------------------------------------
// Shared helper modules
// ---------------------------------------------------------------------------

// Rounded box, used everywhere. Centred in X/Y, sitting on Z=0.
module rbox(x, y, z, r = 3) {
    hull() for (dx = [-1, 1], dy = [-1, 1])
        translate([dx * (x/2 - r), dy * (y/2 - r), 0])
            cylinder(r = r, h = z);
}

// Screw hole with a counterbore for the head.
module m3_cbore(depth, head_depth = m3_head_h) {
    cylinder(d = m3_clear, h = depth + 0.2, center = false);
    translate([0, 0, depth - head_depth])
        cylinder(d = m3_head_d, h = head_depth + 0.2);
}

// Heat-set insert boss. Print the bore undersize; the brass makes its own.
module insert_boss(h = insert_m3_h + 2, wall = 2.2) {
    difference() {
        cylinder(d = insert_m3_d + wall * 2, h = h);
        translate([0, 0, h - insert_m3_h])
            cylinder(d = insert_m3_d, h = insert_m3_h + 0.1);
    }
}

// A 45-degree chamfer ring, so top edges are not sharp.
module chamfer_ring(d, c = 1.2) {
    difference() {
        cylinder(d = d + 2, h = c);
        cylinder(d1 = d, d2 = d - c * 2, h = c + 0.1);
    }
}
