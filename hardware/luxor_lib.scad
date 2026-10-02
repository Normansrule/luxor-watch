// Luxor case — geometry library.  All modules build in the WATCH FRAME
// (see luxor_params.scad); the numbered part files only re-orient for
// printing.  Nothing here should need editing to fit your board: change
// luxor_params.scad instead.

include <luxor_params.scad>

EPS = 0.01;

// ------------------------------------------------------------ helpers

// Place children at angle a (deg from 3 o'clock, CCW) on radius r, with
// local +X pointing radially outward and local +Y tangential.
module at_angle(a, r = 0, z = 0) {
    rotate([0, 0, a]) translate([r, 0, z]) children();
}

// Quarter-circle points for a rounded profile corner.
function arc(cx, cz, r, a0, a1, n = 12) =
    [for (i = [0 : n]) let (a = a0 + (a1 - a0) * i / n) [cx + r * cos(a), cz + r * sin(a)]];

// Horizontal capsule (stadium) in the XZ plane, extruded along Y.
module capsule_y(w, h, len) {
    rotate([90, 0, 0])
        linear_extrude(height = len, center = true)
            hull() {
                translate([-(w - h) / 2, 0]) circle(d = h);
                translate([ (w - h) / 2, 0]) circle(d = h);
            }
}

// ------------------------------------------------------------ shared cuts

// USB-C cable mouth at 6 o'clock.  The same solid is cut from the shell and
// the back cover, so the opening is continuous across the split.
module usb_mouth() {
    translate([0, -(bore_r + shell_wall / 2), -usb_depth])
        capsule_y(cable_w + 2 * mouth_clear, cable_h + 2 * mouth_clear, shell_wall + 6);
}

// =================================================================== SHELL

// Cross-section of the shell ring in (r, z), revolved about Z.
function shell_profile() =
    let (r_open = lip_id / 2, top = case_front_z, top_in = watch_front_z)
    concat(
        [[r_open + lip_chamfer, top_in]],
        bezel_ring
          ? [[bezel_ir, top_in], [bezel_ir, top],               // inlay pocket: inner wall,
             [case_r - bezel_rim, top], [case_r - bezel_rim, top_in],  // floor, outer wall,
             [case_r - 0.3, top_in], [case_r, top_in - 0.3]]    // black rim with a crisp edge
          : arc(case_r - 1.0, top - 1.0, 1.0, 90, 0, 12),      // rounded front edge
        [[case_r, z_split + 0.35], [case_r - 0.35, z_split]],  // 0.35 seam chamfer
        [[bore_r, z_split], [bore_r, lip_under_z], [r_open, lip_under_z],
         [r_open, top_in - lip_chamfer]]);

lug_top = lug_root_top;

// One lug horn, in (y, z) then extruded along X.  Top face drops at 45°
// so the shell prints face-down without supports.
module lug_horn() {
    tip_y = case_r + lug_len;
    slope_y0 = case_r + 1.0;
    tip_top = lug_top - (tip_y - slope_y0);
    rotate([90, 0, 90])
        linear_extrude(height = lug_w, center = true)
            offset(r = 0.6) offset(delta = -0.6)
                polygon([[case_r - 5.0, lug_back], [tip_y, lug_back],
                         [tip_y, tip_top], [slope_y0, lug_top], [case_r - 5.0, lug_top]]);
}

module lugs() {
    for (sy = [1, -1]) for (sx = [1, -1])
        mirror([0, sy < 0 ? 1 : 0, 0])
            translate([sx * (strap_w / 2 + lug_w / 2), 0, 0]) lug_horn();
}

module spring_bar_holes() {
    for (sy = [1, -1]) for (sx = [1, -1])
        translate([sx * (strap_w / 2 + bar_hole_depth / 2 - EPS),
                   sy * (case_r + bar_y_from_edge), bar_z])
            rotate([0, 90, 0]) cylinder(d = bar_hole_d, h = bar_hole_depth + 2 * EPS, center = true, $fn = 24);
}

// Seamless flexure button: a U-slot through the wall frees a tab hinged at
// its front end; the tab is thinned from inside and carries a nub that
// presses the board's switch.
module flexure_cut(a) {
    zc = -button_depth;
    z_free = zc - 1.5;                 // free end just behind the switch
    z_hinge = z_free + flex_len;
    at_angle(a) {
        // U-slot
        difference() {
            translate([bore_r - 1, -(flex_w / 2 + flex_slot), z_free - flex_slot])
                cube([shell_wall + 2, flex_w + 2 * flex_slot, flex_len + flex_slot]);
            translate([bore_r - 2, -flex_w / 2, z_free])
                cube([shell_wall + 4, flex_w, flex_len + 1]);
        }
        // thin the tab from inside
        translate([bore_r - 1, -flex_w / 2 - EPS, z_free - EPS])
            cube([1 + shell_wall - flex_t, flex_w + 2 * EPS, flex_len - 0.6]);
    }
}

module flexure_nub(a) {
    at_angle(a)
        translate([bore_r + (shell_wall - flex_t) + EPS, 0, -button_depth])
            rotate([0, -90, 0])
                cylinder(d1 = flex_nub_d, d2 = flex_nub_d - 0.6,
                         h = (shell_wall - flex_t) + flex_reach, $fn = 32);
}

module shell() {
    union() {
        difference() {
            union() {
                rotate_extrude($fn = 180) polygon(shell_profile());
                difference() {                 // lugs, trimmed to the bore
                    lugs();
                    translate([0, 0, z_split - 1]) cylinder(r = bore_r + fit_clear / 2, h = 30);
                }
            }
            usb_mouth();
            spring_bar_holes();
            // rotation key notch in the rear rim at 12 o'clock
            translate([-(key_w + 0.3) / 2, bore_r - 0.5, z_split - 1])
                cube([key_w + 0.3, 0.5 + key_depth, 1 + key_h + 0.2]);
            if (button_style == "flexure") for (a = button_angles) flexure_cut(a);
            at_angle(mic_angle, 0, -mic_depth)
                translate([bore_r - 1, 0, 0]) rotate([0, 90, 0]) cylinder(d = mic_d, h = shell_wall + 2, $fn = 20);
            // 12 o'clock index: a fine engraved tick on the front face
            if (!bezel_ring) translate([-0.35, lip_id / 2 + lip_chamfer + 0.6, case_front_z - 0.3])
                cube([0.7, 1.4, 1]);
        }
        if (button_style == "flexure") for (a = button_angles) flexure_nub(a);
    }
}

// ============================================================== BEZEL RING

module bezel() {
    r_in = bezel_ir + bezel_fit / 2;
    r_out = case_r - bezel_rim - bezel_fit / 2;
    z0 = case_front_z;
    top = z0 + bezel_t;
    rotate_extrude($fn = 180)
        polygon(concat(
            [[r_in, z0], [r_out, z0]],
            arc(r_out - 0.3, top - 0.3, 0.3, 0, 90, 8),
            [[r_in + 0.25, top], [r_in, top - 0.25]]));
}

// ============================================================== BACK COVER

module cover_body() {
    zb = cover_back_z;
    rotate_extrude($fn = 180)
        polygon([[0, zb], [case_r - back_chamfer, zb], [case_r, zb + back_chamfer],
                 [case_r, z_split - 0.35], [case_r - 0.35, z_split], [0, z_split]]);
}

module back_engraving() {
    zb = cover_back_z;
    // mirrored so it reads correctly when you look at the back of the watch
    translate([0, 0, zb - EPS]) mirror([1, 0, 0]) linear_extrude(height = engrave_d + EPS) {
        for (d = [28, 30.2]) difference() { circle(d = d + 0.5); circle(d = d); }
        translate([0, 3.2]) text(back_text, size = 4.2, halign = "center", valign = "center",
                                 font = "Liberation Sans:style=Bold", spacing = 1.35);
        translate([0, -2.6]) text(back_subtext, size = 1.9, halign = "center", valign = "center",
                                  font = "Liberation Sans", spacing = 1.15);
    }
}

module cover() {
    zf = cover_back_z + cover_floor;                 // inside floor
    difference() {
        union() {
            difference() {
                cover_body();
                translate([0, 0, zf]) cylinder(r = bore_r, h = 20);
            }
            // standoff landing bosses
            for (p = standoff_xy) translate([p[0], p[1], zf - EPS])
                cylinder(d = boss_d, h = -standoff_depth - zf + EPS, $fn = 48);
            // battery end rails (the bosses locate it along Y)
            for (sx = [1, -1])
                translate([sx * (batt_l / 2 + batt_rail_t / 2), batt_cy, zf + batt_rail_h / 2 - EPS])
                    cube([batt_rail_t, batt_w * 0.6, batt_rail_h], center = true);
            // rotation key at 12 o'clock
            translate([-key_w / 2, bore_r, z_split - EPS]) cube([key_w, key_depth - 0.15, key_h]);
        }
        // screw holes + counterbores from the back
        for (p = standoff_xy) translate([p[0], p[1], 0]) {
            translate([0, 0, cover_back_z - 1]) cylinder(d = m2_clear_d, h = 40, $fn = 32);
            translate([0, 0, cover_back_z - 1]) cylinder(d = m2_head_d, h = 1 + m2_head_h, $fn = 48);
        }
        usb_mouth();
        // speaker slots, through the cover wall
        n = speaker_slots;
        for (i = [0 : n - 1]) at_angle(speaker_angle + (i - (n - 1) / 2) * 5.2)
            translate([bore_r - 1, -0.5, (z_split + zf) / 2 - 1.5]) cube([shell_wall + 2, 1.0, 3.0]);
        // battery lead notch in the floor rails area: a shallow channel to 6 o'clock
        translate([-2, -bore_r + 1, zf - 0.4]) cube([4, bore_r - batt_w / 2 + batt_cy, 0.4 + EPS]);
        back_engraving();
    }
}

// =================================================================== DOCK
// Upright nightstand: the watch stands on its 6 o'clock edge, leaning back
// dock_tilt degrees, and drops onto a USB-C plug held pointing up in a
// pocket.  Remove the lower strap half first (quick-release).

dock_tilt    = 12;      // DESIGN
dock_c_z     = 48;      // DESIGN  height of the watch centre above the desk
plug_len     = 18;      // MEASURE overmould length of your USB-C cable (not the metal tip)
cable_d      = 4.2;     // MEASURE cable diameter
dock_w       = 66;
dock_d       = 58;

// Watch frame -> dock world frame.
module docked() {
    translate([0, 0, dock_c_z]) rotate([-dock_tilt, 0, 0]) rotate([90, 0, 0]) children();
}

module watch_envelope() {
    c = 0.5;
    translate([0, 0, cover_back_z - c]) cylinder(r = case_r + c, h = watch_front_z - cover_back_z + 2 * c);
    for (sy = [1, -1]) for (sx = [1, -1])
        translate([sx * (strap_w / 2 + lug_w / 2), sy * (case_r + lug_len / 2 - 2.5), (lug_back + lug_top) / 2])
            cube([lug_w + 2 * c, lug_len + 6, lug_top - lug_back + 2 * c], center = true);
}

module plug_pocket() {
    // overmould pocket continues the watch's USB axis straight down
    translate([0, -case_r + 0.2, -usb_depth])
        rotate([90, 0, 0]) linear_extrude(height = plug_len + 0.5)
            offset(r = 1) square([cable_w + 0.4 - 2, cable_h + 0.4 - 2], center = true);
}

dock_base_h  = 12;      // DESIGN

module dock() {
    // the point of the watch's 6 o'clock edge, in dock coordinates
    difference() {
        union() {
            // slim base: 45° foot chamfer, softened top edge
            hull() {
                linear_extrude(height = 2) offset(r = 10) offset(delta = -10)
                    square([dock_w - 4, dock_d - 4], center = true);
                translate([0, 0, 2]) linear_extrude(height = dock_base_h - 3) offset(r = 10) offset(delta = -10)
                    square([dock_w, dock_d], center = true);
                translate([0, 0, dock_base_h - 1]) linear_extrude(height = 1) offset(r = 9) offset(delta = -9)
                    square([dock_w - 2, dock_d - 2], center = true);
            }
            // pedestal under 6 o'clock, between the lugs: carries the plug
            hull() {
                translate([-(strap_w - 2) / 2, -14, 0]) cube([strap_w - 2, 26, dock_base_h]);
                docked() translate([-(strap_w - 2) / 2, -case_r - 1, cover_back_z + 1])
                    cube([strap_w - 2, 6, watch_front_z - cover_back_z - 2.5]);
            }
            // backrest behind the watch
            hull() {
                translate([-14, 8, 0]) cube([28, 18, dock_base_h]);
                docked() translate([0, 6, cover_back_z - 3.5]) cylinder(r = 13, h = 3.5);
            }
        }
        docked() watch_envelope();
        docked() plug_pocket();
        // cable: straight down from the pocket and out under the base
        docked() translate([0, -case_r - plug_len, -usb_depth]) rotate([90, 0, 0]) cylinder(d = cable_d + 0.6, h = 40, $fn = 24);
        // cable groove under the base, with a 45° peaked roof (prints without support)
        translate([0, 0, -EPS]) hull() {
            for (y = [-8, dock_d]) translate([0, y, 0]) {
                cylinder(d = cable_d + 0.8, h = cable_d * 0.6, $fn = 24);
                translate([0, 0, cable_d * 0.6]) cylinder(d1 = cable_d + 0.8, d2 = 0.1, h = (cable_d + 0.8) / 2, $fn = 24);
            }
        }
        // two pockets under the base for weight (M8 steel washers, 16 mm OD) or
        // 15 mm rubber feet; 45° conical roofs so nothing bridges
        for (sx = [1, -1]) translate([sx * 20, -10, -EPS]) {
            cylinder(d = 16.4, h = 3.2);
            translate([0, 0, 3.2 - EPS]) cylinder(d1 = 16.4, d2 = 0.1, h = 8.2);
        }
    }
}

// ============================================================ REFERENCES

// Simplified board + glass, for the assembly view only.
module board_ghost() {
    color([0.05, 0.05, 0.06]) translate([0, 0, -1.2]) cylinder(d = glass_od, h = 1.2);         // glass
    color([0.02, 0.1, 0.2]) translate([0, 0, -0.05]) cylinder(d = active_d, h = 0.06);       // screen
    color([0.1, 0.35, 0.15]) translate([0, 0, -board_depth + depth_clear]) cylinder(d = board_od - 0.4, h = board_depth - 1.2 - depth_clear);
    color([0.8, 0.65, 0.3]) for (p = standoff_xy) translate([p[0], p[1], -standoff_depth]) cylinder(d = 3.2, h = 3, $fn = 24);
}

module battery_ghost() {
    color([0.25, 0.3, 0.45])
        translate([-batt_l / 2, batt_cy - batt_w / 2, cover_back_z + cover_floor + 0.1]) cube([batt_l, batt_w, batt_t]);
}
