// =============================================================================
//  eye_and_sensors.scad - the eyeball, its gimbal, and the sensor brackets.
//
//    part = "eye_ball"     x1 - hemisphere shell, takes the lens
//    part = "eye_lens"     x1 - print in clear PETG, or use a 26 mm acrylic dome
//    part = "eye_yoke"     x1 - tilt yoke, carries the ball
//    part = "eye_pan_base" x1 - pan servo mount, carries the yoke
//    part = "tof_bracket"  x2 - VL53L0X breakout bracket (throat + approach)
//    part = "oled_bezel"   x1 - SSD1306 frame, snaps into the front panel
//
//  The eye is the highest-value part of the whole build per gram of
//  filament: an audience reads "it is looking at me" long before it reads
//  any of the electronics. Print the ball in a matte colour and the lens
//  glossy - the contrast is what sells it.
// =============================================================================

include <binchad_params.scad>

part = "all";

// ---------------------------------------------------------------------------
module eye_ball() {
    shell = 2.4;
    difference() {
        sphere(d = eye_ball_d);
        // hollow
        sphere(d = eye_ball_d - shell * 2);
        // flatten the back half - printable, and gives the yoke something
        // to grip
        translate([0, 0, -eye_ball_d]) cube(eye_ball_d * 2, center = true);
        // lens aperture, facing +X
        rotate([0, 90, 0])
            translate([0, 0, eye_ball_d/2 - 3])
                cylinder(d = eye_lens_d, h = 6);
        // lens seat lip
        rotate([0, 90, 0])
            translate([0, 0, eye_ball_d/2 - 4.6])
                cylinder(d = eye_lens_d + 3, h = 1.8);
    }
    // trunnions for the tilt axis
    for (dy = [-1, 1])
        translate([0, dy * (eye_ball_d/2 - 1), 0])
            rotate([90, 0, 0])
                cylinder(d = 5 - fit_loose, h = 5, center = true);
}

module eye_lens() {          // clear PETG, 3 perimeters, no infill
    difference() {
        union() {
            cylinder(d = eye_lens_d - fit_snug, h = 2);
            translate([0, 0, 2]) scale([1, 1, 0.45]) sphere(d = eye_lens_d - 2);
        }
        translate([0, 0, -0.1]) cylinder(d = eye_lens_d - 6, h = 1.2);
    }
}

// ---------------------------------------------------------------------------
module eye_yoke() {
    arm_h = eye_ball_d/2 + 6;
    difference() {
        union() {
            rbox(eye_ball_d + 14, 20, 5, 3);
            for (dy = [-1, 1])
                translate([-(eye_ball_d + 14)/2 + 4, dy * (eye_ball_d/2 + 2) - 2.5, 0])
                    cube([eye_ball_d + 6, 5, arm_h]);
        }
        // tilt bearing bores
        for (dy = [-1, 1])
            translate([0, dy * (eye_ball_d/2 + 2), arm_h - 4])
                rotate([90, 0, 0])
                    cylinder(d = 5 + fit_loose, h = 8, center = true);
        // tilt servo horn slot on one arm
        translate([-3, eye_ball_d/2 - 1, arm_h - 4])
            rotate([90, 0, 0]) cylinder(d = 8, h = 3);
        // pan servo shaft
        translate([0, 0, -0.1]) cylinder(d = 5.8, h = 5.2);
        translate([-4, -2.6, 3.4]) cube([20, 5.2, 1.8]);   // horn pocket
    }
}

module eye_pan_base() {
    difference() {
        union() {
            rbox(46, 34, 4, 3);
            // micro servo cradle standing up
            translate([-servo_micro_x/2 - 2, -servo_micro_y/2 - 2, 4])
                cube([servo_micro_x + 4, servo_micro_y + 4, servo_micro_z + 2]);
        }
        translate([-servo_micro_x/2, -servo_micro_y/2, 3.9])
            cube([servo_micro_x, servo_micro_y, servo_micro_z + 3]);
        for (dx = [-1, 1])
            translate([dx * servo_micro_tab_x/2, 0, 4 + servo_micro_z - 5])
                rotate([90, 0, 0])
                    cylinder(d = servo_micro_hole_d, h = 40, center = true);
        for (dx = [-1, 1])
            translate([dx * 19, 0, -0.1]) m3_cbore(4.2);
    }
}

// ---------------------------------------------------------------------------
// ToF BRACKET
// The throat sensor points straight down; the approach sensor points out at
// roughly chest height. Same bracket, mounted on different faces.
// The 4 mm standoff keeps the VL53L0X's cover glass clear of the plastic -
// a bracket that shadows the emitter is the single most common cause of
// "the sensor reads 8190 forever".
// ---------------------------------------------------------------------------
module tof_bracket() {
    difference() {
        union() {
            rbox(vl53_pcb_x + 10, vl53_pcb_y + 10, 3, 3);
            for (dx = [-1, 1])
                translate([dx * vl53_hole_span/2, 0, 3])
                    cylinder(d = 5.5, h = 4);
        }
        // sensor window - generous, do not let the print shadow the emitter
        translate([0, 0, -0.1]) rbox(vl53_pcb_x - 6, vl53_pcb_y - 6, 3.2, 2);
        for (dx = [-1, 1])
            translate([dx * vl53_hole_span/2, 0, 3 - 0.1])
                cylinder(d = vl53_hole_d, h = 4.2);
        for (dy = [-1, 1])
            translate([0, dy * (vl53_pcb_y/2 + 3), -0.1])
                cylinder(d = m3_clear, h = 3.2);
    }
}

// ---------------------------------------------------------------------------
module oled_bezel() {
    difference() {
        rbox(oled_pcb_x + 12, oled_pcb_y + 12, 4, 3);
        // viewing window
        translate([0, 3.5, -0.1]) rbox(oled_win_x + 1, oled_win_y + 1, 4.2, 1);
        // PCB recess from the back
        translate([0, 0, 4 - 2.0])
            rbox(oled_pcb_x + fit_clear, oled_pcb_y + fit_clear, 2.2, 1);
        for (dx = [-1, 1], dy = [-1, 1])
            translate([dx * oled_hole_span_x/2, dy * oled_hole_span_y/2, -0.1])
                cylinder(d = 2.4, h = 4.2);
    }
}

// ---------------------------------------------------------------------------
if (part == "eye_ball")          eye_ball();
else if (part == "eye_lens")     eye_lens();
else if (part == "eye_yoke")     eye_yoke();
else if (part == "eye_pan_base") eye_pan_base();
else if (part == "tof_bracket")  tof_bracket();
else if (part == "oled_bezel")   oled_bezel();
else {
    translate([  0,  0, 0]) eye_ball();
    translate([ 60,  0, 0]) eye_yoke();
    translate([130,  0, 0]) eye_pan_base();
    translate([  0, 70, 0]) tof_bracket();
    translate([ 60, 70, 0]) oled_bezel();
    translate([130, 70, 0]) eye_lens();
}
