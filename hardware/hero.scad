// Product render of the assembled watch on a strap — for README images
// only (nothing here is printed).  renders/make_hero.py drives it and puts
// the real UI frame on the glass.
//   finish = "noir" | "ivory"
include <luxor_lib.scad>

finish = "noir";
case_col  = finish == "noir" ? [0.08, 0.08, 0.085] : [0.93, 0.91, 0.86];
strap_col = finish == "noir" ? [0.10, 0.09, 0.08] : [0.55, 0.38, 0.24];  // black / tan leather
gold      = [0.83, 0.69, 0.27];

// One strap half: leaves the spring bar, runs outward and curls back
// towards the wrist like a strap on a curved arm.
module strap_half(sy) {
    w = strap_w - 0.4; t = 2.2;
    // starts tucked against the case, runs 60 mm out and curls back
    pts = [ for (i = [0:12]) let(a = i * 6)
              [ sy * (case_r + 0.6 + 60 * sin(a) / sin(72)),
                bar_z - 16 * (1 - cos(a)) ] ];
    for (i = [0:len(pts) - 2])
        hull() for (p = [pts[i], pts[i + 1]])
            translate([0, p[0], p[1]]) rotate([0, 90, 0])
                cylinder(d = t, h = w, center = true, $fn = 16);
}

color(case_col) shell();
if (bezel_ring) color(gold) bezel();
color(case_col) cover();
// glass and screen (the screen disc is replaced by the real UI in make_hero.py)
color([0.02, 0.02, 0.025]) translate([0, 0, -1.2]) cylinder(d = glass_od - 0.2, h = 1.19);
// glass face, drawn just in front of the real glass so no case surface shows through
color([0.02, 0.02, 0.025]) translate([0, 0, 0.12]) cylinder(d = lip_id + 0.4, h = 0.02);
color([0.0, 0.0, 1.0]) translate([0, 0, 0.15]) cylinder(d = active_d, h = 0.05);   // key colour
color(strap_col) { strap_half(1); strap_half(-1); }
