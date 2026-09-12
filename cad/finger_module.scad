// =============================================================================
//  finger_module.scad - the useless-box mechanism.
//
//  A self-contained cartridge: two micro servos, a hinged hatch and an
//  oversized finger. It bolts to the bin as one unit and can be pulled out
//  and swapped in under a minute, which matters because this is the part
//  most likely to be broken by an enthusiastic audience.
//
//    part = "box"        x1  - the compartment, print open-face-down
//    part = "door"       x1  - the hatch
//    part = "finger"     x1  - the finger arm
//    part = "finger_tip" x1  - TPU tip, soft contact with the switch
//    part = "servo_plate"x1  - the two micro-servo carrier
//    part = "faceplate"  x1  - cosmetic front with the warning label recess
//
//  Geometry note: the finger pivots about the extend servo's shaft, which
//  sits finger_len*0.28 back from the hatch. At FINGER_ANGLE_PRESS (112 deg
//  in the firmware) the tip lands about 6 mm past the switch face - the
//  overshoot is absorbed by the TPU tip, so the servo stalls softly instead
//  of stripping its gears on a switch that did not move.
// =============================================================================

include <binchad_params.scad>

part = "all";

pivot_back = finger_len * 0.28;

// ---------------------------------------------------------------------------
module finger_box() {
    wall = 2.6;
    difference() {
        rbox(finger_box_x, finger_box_y, finger_box_z, 4);

        // hollow
        translate([0, 0, wall])
            rbox(finger_box_x - wall*2, finger_box_y - wall*2,
                 finger_box_z, 3);

        // hatch aperture in the front face (+Y)
        translate([-(door_x + fit_clear)/2, finger_box_y/2 - wall - 0.2,
                   finger_box_z - door_y - 8])
            cube([door_x + fit_clear, wall + 1.0, door_y + fit_clear]);

        // hinge pin bores for the hatch
        for (dx = [-1, 1])
            translate([dx * (door_x/2 + 2), finger_box_y/2 - wall/2,
                       finger_box_z - 8])
                rotate([0, 90, 0])
                    cylinder(d = 2.0 + fit_loose, h = 6, center = true);

        // cable gland at the back
        translate([0, -finger_box_y/2 - 0.1, 10])
            rotate([-90, 0, 0]) cylinder(d = 9, h = wall + 0.4);

        // mounting holes to the bin body (from the back face)
        for (dx = [-1, 1], dz = [12, finger_box_z - 12])
            translate([dx * (finger_box_x/2 - 9), -finger_box_y/2 - 0.1, dz])
                rotate([-90, 0, 0]) cylinder(d = m3_clear, h = wall + 0.4);
    }

    // insert bosses for the servo plate, standing on the floor
    for (dx = [-1, 1], dy = [-1, 1])
        translate([dx * (finger_box_x/2 - 8), dy * (finger_box_y/2 - 9), 2.6])
            insert_boss();
}

// ---------------------------------------------------------------------------
module finger_door() {
    difference() {
        union() {
            rbox(door_x, door_y, door_t, 2);
            // hinge knuckles
            for (dx = [-1, 1])
                translate([dx * (door_x/2 + 1), door_y/2 - 3, door_t/2])
                    rotate([0, 90, 0])
                        cylinder(d = 5, h = 4, center = true);
        }
        for (dx = [-1, 1])
            translate([dx * (door_x/2 + 1), door_y/2 - 3, door_t/2])
                rotate([0, 90, 0])
                    cylinder(d = 2.0 + fit_snug, h = 6, center = true);
        // servo horn slot on the inside face
        translate([-1.5, -door_y/2 + 4, -0.1]) cube([3, 10, 1.4]);
    }
}

// ---------------------------------------------------------------------------
// THE FINGER
// Oversized, blunt, and rounded on every edge that can reach a person.
// ---------------------------------------------------------------------------
module finger_arm() {
    difference() {
        union() {
            // shaft, tapering slightly toward the tip
            hull() {
                translate([0, 0, 0]) cylinder(d = finger_w, h = finger_t);
                translate([finger_len - finger_tip_r, 0, 0])
                    cylinder(d = finger_w * 0.8, h = finger_t);
            }
            // knuckle detail, purely so it reads as a finger from 5 m away
            translate([finger_len * 0.45, 0, finger_t])
                scale([1.6, 1, 0.5]) sphere(d = finger_w * 0.7);
            // servo horn boss
            cylinder(d = finger_w + 4, h = finger_t + 3);
        }
        // servo output shaft + horn screw
        translate([0, 0, -0.1]) cylinder(d = 5.8, h = finger_t + 3.2);
        translate([0, 0, finger_t + 3 - 2])
            cylinder(d = horn_screw_d + fit_snug, h = 3);
        // horn seat: a flat-bottomed pocket for a single-arm horn
        translate([-4, -2.6, -0.1]) cube([20, 5.2, 1.6]);
        // socket for the TPU tip
        translate([finger_len - finger_tip_r * 2, 0, -0.1])
            cylinder(d = finger_w * 0.55, h = finger_t + 0.2);
    }
}

module finger_tip_tpu() {    // print in TPU, 100% infill
    union() {
        cylinder(d = finger_w * 0.55 - fit_snug, h = finger_t);   // plug
        translate([0, 0, finger_t]) sphere(r = finger_tip_r);
    }
}

// ---------------------------------------------------------------------------
// SERVO PLATE - carries the door servo and the extend servo
// ---------------------------------------------------------------------------
module servo_plate() {
    plate_x = finger_box_x - 20;
    plate_y = finger_box_y - 20;

    difference() {
        rbox(plate_x, plate_y, 4, 3);

        // extend servo pocket (drives the finger)
        translate([-servo_micro_x/2 + 6, -servo_micro_y/2, -0.1])
            cube([servo_micro_x, servo_micro_y, 4.2]);
        for (dy = [-1, 1])
            translate([6 + dy * servo_micro_tab_x/2, 0, -0.1])
                cylinder(d = servo_micro_hole_d, h = 4.2);

        // door servo pocket
        translate([-servo_micro_x/2 - 4, plate_y/2 - servo_micro_y - 6, -0.1])
            cube([servo_micro_x, servo_micro_y, 4.2]);

        // fixing holes matching the box's insert bosses
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * (plate_x/2 - 4), dy * (plate_y/2 - 4), -0.1])
                m3_cbore(4.2);
    }
}

// ---------------------------------------------------------------------------
// FACEPLATE - the bit the audience actually looks at
// ---------------------------------------------------------------------------
module faceplate() {
    difference() {
        rbox(finger_box_x + 8, finger_box_z + 8, 3, 4);
        // hatch opening
        translate([0, 12, -0.1]) rbox(door_x + 2, door_y + 2, 3.2, 2);
        // recessed label panel: print a 0.6 mm inlay in a second colour,
        // or fill with a sticker. Text is not modelled - it never prints
        // cleanly at this size.
        translate([0, -18, 3 - 0.6]) rbox(52, 14, 0.8, 2);
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * (finger_box_x/2 - 2), dy * (finger_box_z/2 - 2), -0.1])
                m3_cbore(3.2, 2.0);
    }
}

// ---------------------------------------------------------------------------
if (part == "box")          finger_box();
else if (part == "door")         finger_door();
else if (part == "finger")       finger_arm();
else if (part == "finger_tip")   finger_tip_tpu();
else if (part == "servo_plate")  servo_plate();
else if (part == "faceplate")    faceplate();
else {
    translate([   0,   0, 0]) finger_box();
    translate([  80,   0, 0]) finger_door();
    translate([  80,  40, 0]) finger_arm();
    translate([  80, -40, 0]) servo_plate();
    translate([ -90,   0, 0]) faceplate();
}
