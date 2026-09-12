// =============================================================================
//  chassis_and_tray.scad - electronics tray, button housing, feet, panels.
//
//    part = "tray"          x1 - slide-out electronics tray
//    part = "tray_rails"    x2 - rails that the tray slides on
//    part = "button_plate"  x1 - NORMAL MODE switch mount
//    part = "service_panel" x1 - removable rear access panel
//    part = "foot"          x4 - TPU foot
//    part = "led_diffuser"  x1 - strip diffuser for the WS2812 ring
//    part = "speaker_grille"x1 - speaker cover
//
//  Serviceability rule for this whole project: nothing is glued and nothing
//  is buried. Every module comes out on M3 screws into heat-set inserts, and
//  the tray slides out of the back with the wiring still attached. At a
//  hackathon you will be debugging this on a table with ten minutes to go,
//  and a bin you have to destroy to open is a bin you cannot fix.
// =============================================================================

include <binchad_params.scad>

part = "all";

// ---------------------------------------------------------------------------
module tray() {
    difference() {
        union() {
            rbox(tray_x, tray_y, tray_z, 4);
            // low retaining lip
            difference() {
                rbox(tray_x, tray_y, tray_z + 8, 4);
                translate([0, 0, tray_z])
                    rbox(tray_x - tray_wall*2, tray_y - tray_wall*2, 10, 3);
            }
            // rail tongues
            for (dx = [-1, 1])
                translate([dx * (tray_x/2 + tray_rail_w/2 - 0.5) - tray_rail_w/2, -tray_y/2, 0])
                    cube([tray_rail_w, tray_y, 3]);
            // pull handle
            translate([0, -tray_y/2 - 6, 0]) rbox(40, 14, 6, 3);
        }

        // ESP32-S3 DevKitC-1 mounting pattern (nominal 25.4 x 52.1 hole span)
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * 25.4/2 - 40, dy * 52.1/2, -0.1])
                cylinder(d = m3_tap, h = tray_z + 0.2);

        // Generic perfboard / buck / amp mounting grid, 10 mm pitch.
        // Drill what you need; the rest is ventilation.
        for (gx = [-2 : 4], gy = [-4 : 4])
            translate([gx * 15 + 15, gy * 11, -0.1])
                cylinder(d = 3.2, h = tray_z + 0.2);

        // cable pass-throughs
        for (dy = [-1, 1])
            translate([tray_x/2 - 12, dy * 30, -0.1])
                hull() for (dx = [0, 14]) translate([dx - 7, 0, 0])
                    cylinder(d = 10, h = tray_z + 0.2);

        // handle finger slot
        translate([0, -tray_y/2 - 6, 2]) rbox(28, 8, 6, 2);
    }
}

module tray_rails() {
    difference() {
        cube([12, tray_y + 10, 10]);
        // slot the tray tongue rides in
        translate([-0.1, -0.1, 3.5]) cube([tray_rail_w + fit_clear + 0.1, tray_y + 10.2, 3.4]);
        for (dy = [20, tray_y - 10])
            translate([8, dy, -0.1]) cylinder(d = m3_clear, h = 10.2);
    }
}

// ---------------------------------------------------------------------------
// BUTTON PLATE
// The NORMAL MODE switch and, critically, the geometry that puts it within
// reach of the finger. finger_reach below is the number to check on
// assembly: the switch face must sit inside the finger's arc.
// ---------------------------------------------------------------------------
finger_reach = finger_len + 6;    // tip travel from the hatch

module button_plate() {
    difference() {
        union() {
            rbox(button_plate_x, button_plate_y, button_plate_t, 5);
            // raised collar so the switch body is supported square
            cylinder(d = button_hole_d + 8, h = button_plate_t + 2);
        }
        translate([0, 0, -0.1])
            cylinder(d = button_hole_d, h = button_plate_t + 2.2);
        // anti-rotation notch found on most 19 mm latching switches
        translate([button_hole_d/2 - 0.6, -1.6, -0.1])
            cube([2.4, 3.2, button_plate_t + 2.2]);
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * (button_plate_x/2 - 6), dy * (button_plate_y/2 - 6), -0.1])
                m3_cbore(button_plate_t + 0.2);
    }
    // Assembly aid: a witness mark showing where the finger tip should land.
    // Printed 0.4 mm proud - sand it off once you are happy with the reach.
    translate([0, 0, button_plate_t + 2])
        difference() {
            cylinder(d = button_hole_d + 6, h = 0.4);
            cylinder(d = button_hole_d + 4, h = 0.5);
        }
}

// ---------------------------------------------------------------------------
module service_panel() {
    difference() {
        rbox(160, 120, 3, 5);
        // ventilation slots - servos and the buck converter both like air
        for (gy = [-3 : 3])
            translate([0, gy * 14, -0.1])
                hull() for (dx = [-1, 1])
                    translate([dx * 55, 0, 0]) cylinder(d = 7, h = 3.2);
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * 72, dy * 52, -0.1]) m3_cbore(3.2, 2.0);
        // USB-C service cut-out, so the board can be flashed without opening
        translate([-58, -52, -0.1]) cube([14, 10, 3.2]);
    }
}

// ---------------------------------------------------------------------------
module foot() {              // TPU, 30% infill - it should squash a little
    difference() {
        union() {
            cylinder(d1 = 34, d2 = 28, h = 10);
            translate([0, 0, 10]) cylinder(d = 20, h = 4);
        }
        translate([0, 0, 10 - 0.1]) cylinder(d = insert_m3_d + 0.4, h = 6);
        // concave underside, so it grips rather than skates
        translate([0, 0, -3]) scale([1, 1, 0.35]) sphere(d = 30);
    }
}

// ---------------------------------------------------------------------------
module led_diffuser() {
    len = 180; w = 14;
    difference() {
        union() {
            translate([-len/2, -w/2, 0]) cube([len, w, 3]);
            translate([-len/2, 0, 3]) rotate([0, 90, 0])
                cylinder(d = w, h = len);
        }
        // channel for a 10 mm WS2812 strip
        translate([-len/2 - 0.1, -5.5, -0.1]) cube([len + 0.2, 11, 3.2]);
        translate([-len/2 - 0.1, 0, 3]) rotate([0, 90, 0])
            cylinder(d = w - 2.6, h = len + 0.2);
    }
}

module speaker_grille() {
    difference() {
        cylinder(d = 52, h = 3);
        // ring = [radius, hole count]. Paired deliberately - a nested
        // for(r=..,n=..) in OpenSCAD is a cartesian product, not a zip.
        for (ring = [[8, 6], [16, 12]])
            for (i = [0 : ring[1] - 1])
                rotate([0, 0, i * 360/ring[1]])
                    translate([ring[0], 0, -0.1]) cylinder(d = 4, h = 3.2);
        cylinder(d = 5, h = 3.2);
        for (i = [0 : 3])
            rotate([0, 0, 45 + i * 90])
                translate([23, 0, -0.1]) cylinder(d = 2.6, h = 3.2);
    }
}

// ---------------------------------------------------------------------------
if (part == "tray")               tray();
else if (part == "tray_rails")    tray_rails();
else if (part == "button_plate")  button_plate();
else if (part == "service_panel") service_panel();
else if (part == "foot")          foot();
else if (part == "led_diffuser")  led_diffuser();
else if (part == "speaker_grille")speaker_grille();
else {
    translate([   0,    0, 0]) tray();
    translate([ 180,    0, 0]) tray_rails();
    translate([   0,  150, 0]) button_plate();
    translate([ 100,  150, 0]) foot();
    translate([ 200,  150, 0]) speaker_grille();
    translate([   0, -160, 0]) led_diffuser();
}
