// =============================================================================
//  lid_mechanism.scad - hinge brackets, lid frame, servo mount, linkage.
//
//  Set `part` below (or pass -D part=\"hinge_body\" on the command line) and
//  export one STL at a time.
//
//    part = "hinge_body"     x2  - bracket that bolts to the collar
//    part = "hinge_lid"      x2  - bracket that bolts to the lid
//    part = "lid_frame"      x1  - ribbed lid, print upside down
//    part = "servo_mount"    x1  - standard-size servo cradle
//    part = "linkage"        x1  - two-bar link, servo horn to lid
//    part = "lid_stop"       x2  - hard mechanical end stop
//    part = "all"                - laid out for a look, NOT for printing
//
//  Print notes:
//    PETG, 4 perimeters, 30% gyroid for the brackets.
//    The hinge brackets take the full lid load - do not print them at 15%.
//    Lid frame prints top-face-down, no supports needed.
// =============================================================================

include <binchad_params.scad>

part = "all";

// ---------------------------------------------------------------------------
// HINGE - body side. Bolts to the collar, carries the 4 mm pin.
// ---------------------------------------------------------------------------
module hinge_body() {
    base_x = 34; base_y = 22; base_z = 5;
    difference() {
        union() {
            rbox(base_x, base_y, base_z, 3);
            // Upright carrying the pin bore
            translate([0, -hinge_bracket_t/2, 0])
                cube([hinge_boss_d, hinge_bracket_t, hinge_offset_z], center = false);
            translate([hinge_boss_d/2, 0, hinge_offset_z])
                rotate([90, 0, 0])
                    cylinder(d = hinge_boss_d, h = hinge_bracket_t, center = true);
        }
        // Pin bore - loose fit, this is the rotating joint
        translate([hinge_boss_d/2, 0, hinge_offset_z])
            rotate([90, 0, 0])
                cylinder(d = hinge_pin_d + fit_loose, h = hinge_bracket_t + 2, center = true);

        // Two M3 clearance holes into the collar
        for (dx = [-1, 1])
            translate([dx * 11, 0, -0.1])
                m3_cbore(base_z + 0.2);
    }
}

// ---------------------------------------------------------------------------
// HINGE - lid side. Mirror image, shorter upright.
// ---------------------------------------------------------------------------
module hinge_lid() {
    base_x = 30; base_y = 22; base_z = 5;
    difference() {
        union() {
            rbox(base_x, base_y, base_z, 3);
            translate([0, -hinge_bracket_t/2, 0])
                cube([hinge_boss_d, hinge_bracket_t, hinge_offset_z], center = false);
            translate([hinge_boss_d/2, 0, hinge_offset_z])
                rotate([90, 0, 0])
                    cylinder(d = hinge_boss_d, h = hinge_bracket_t, center = true);
        }
        translate([hinge_boss_d/2, 0, hinge_offset_z])
            rotate([90, 0, 0])
                cylinder(d = hinge_pin_d + fit_loose, h = hinge_bracket_t + 2, center = true);
        for (dx = [-1, 1])
            translate([dx * 9, 0, -0.1])
                m3_cbore(base_z + 0.2);
    }
}

// ---------------------------------------------------------------------------
// LID FRAME
// A thin skin plus a rib grid. Ribs are what let the lid stay at 180 g:
// a solid 5 mm lid would be over 400 g and would need a much bigger servo.
// ---------------------------------------------------------------------------
module lid_frame() {
    rib_pitch = 38;
    difference() {
        union() {
            rbox(lid_x, lid_y, lid_t, 6);
            // perimeter rib
            difference() {
                rbox(lid_x, lid_y, lid_t + lid_rib_h, 6);
                translate([0, 0, -0.1])
                    rbox(lid_x - lid_rib_t * 2, lid_y - lid_rib_t * 2,
                         lid_t + lid_rib_h + 0.2, 5);
            }
            // cross ribs
            for (i = [-2 : 2]) {
                translate([i * rib_pitch - lid_rib_t/2, -lid_y/2 + lid_rib_t, lid_t])
                    cube([lid_rib_t, lid_y - lid_rib_t * 2, lid_rib_h]);
                translate([-lid_x/2 + lid_rib_t, i * rib_pitch - lid_rib_t/2, lid_t])
                    cube([lid_x - lid_rib_t * 2, lid_rib_t, lid_rib_h]);
            }
            // hinge bracket pads
            for (dx = [-1, 1])
                translate([dx * hinge_span/2, -lid_y/2 + 16, lid_t])
                    rbox(34, 26, 4, 3);
            // linkage anchor pad
            translate([0, -lid_y/2 + 52, lid_t]) rbox(26, 20, 5, 3);
        }
        // Bracket screw holes - inserts go in from the underside
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * hinge_span/2 + dy * 9, -lid_y/2 + 16, lid_t + 4 - insert_m3_h])
                cylinder(d = insert_m3_d, h = insert_m3_h + 0.1);
        // Linkage ball-joint hole
        translate([0, -lid_y/2 + 52, -0.1])
            cylinder(d = m3_clear, h = lid_t + 6);
    }
}

// ---------------------------------------------------------------------------
// SERVO MOUNT - standard size (MG996R / DS3218)
// Bolts to the inside of the collar. Slotted holes so the linkage geometry
// can be trimmed on assembly without reprinting anything.
// ---------------------------------------------------------------------------
module servo_mount() {
    wall = 4;
    plate_x = servo_std_tab_x + wall * 2 + 12;
    plate_y = servo_std_y + wall * 2;
    plate_z = 5;

    difference() {
        union() {
            rbox(plate_x, plate_y, plate_z, 4);
            // cradle walls
            for (dx = [-1, 1])
                translate([dx * (servo_std_x/2 + wall/2) - wall/2, -plate_y/2, plate_z])
                    cube([wall, plate_y, 20]);
        }
        // servo body pocket
        translate([-servo_std_x/2, -servo_std_y/2, plate_z - 0.1])
            cube([servo_std_x, servo_std_y, 22]);
        // servo tab screws
        for (dx = [-1, 1])
            translate([dx * servo_std_tab_x/2, 0, plate_z + 10])
                rotate([90, 0, 0])
                    cylinder(d = servo_std_hole_d, h = plate_y + 2, center = true);
        // slotted mounting holes to the collar
        for (dx = [-1, 1])
            translate([dx * (plate_x/2 - 7), 0, -0.1])
                hull() for (dy = [-3, 3])
                    translate([0, dy, 0]) cylinder(d = m3_clear, h = plate_z + 0.2);
        // cable exit
        translate([-servo_std_x/2 - wall - 0.1, -5, plate_z + 4])
            cube([wall + 0.2, 10, 8]);
    }
}

// ---------------------------------------------------------------------------
// LINKAGE
// Two holes, link_len apart. Print flat, 100% infill - it is 4 mm thick and
// carries the entire lid load in tension and compression.
// ---------------------------------------------------------------------------
module linkage() {
    difference() {
        hull() for (dx = [0, link_len])
            translate([dx, 0, 0]) cylinder(d = link_w, h = link_t);
        for (dx = [0, link_len])
            translate([dx, 0, -0.1]) cylinder(d = link_hole_d, h = link_t + 0.2);
        // lightening slot, and it looks more like a machine part
        hull() for (dx = [link_len * 0.28, link_len * 0.72])
            translate([dx, 0, -0.1]) cylinder(d = 4, h = link_t + 0.2);
    }
}

// ---------------------------------------------------------------------------
// LID STOP
// A hard mechanical limit at each end of travel, so a firmware bug cannot
// drive the lid into the body. The TPU pad is what stops the clack.
// ---------------------------------------------------------------------------
module lid_stop() {
    difference() {
        union() {
            rbox(24, 16, 6, 3);
            translate([0, 0, 6]) rbox(24, 10, 10, 3);
        }
        for (dx = [-1, 1])
            translate([dx * 8, 0, -0.1]) m3_cbore(6.2);
        // recess for a 2 mm TPU bumper pad (print separately in TPU)
        translate([0, -5.1, 8]) cube([18, 2.2, 8], center = false);
    }
}

// ---------------------------------------------------------------------------
module lid_stop_pad() {           // print in TPU
    rbox(18, 2.0, 8, 1);
}

// ---------------------------------------------------------------------------
if (part == "hinge_body")   hinge_body();
else if (part == "hinge_lid")    hinge_lid();
else if (part == "lid_frame")    lid_frame();
else if (part == "servo_mount")  servo_mount();
else if (part == "linkage")      linkage();
else if (part == "lid_stop")     lid_stop();
else if (part == "lid_stop_pad") lid_stop_pad();
else {
    // Layout view. Not a print plate.
    translate([-120,  60, 0]) hinge_body();
    translate([ -60,  60, 0]) hinge_lid();
    translate([   0, -80, 0]) lid_frame();
    translate([  60,  60, 0]) servo_mount();
    translate([-120,  20, 0]) linkage();
    translate([ -40,  20, 0]) lid_stop();
}
