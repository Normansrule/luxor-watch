// Luxor — assembly and exploded views, for checking fit by eye.
//   explode = 0      assembled
//   explode = 1      parts pulled apart along Z
//   ghosts  = true   show simplified board, glass and battery
//   section = true   cut away the front half (X > 0) to see inside
// Colours: finish = "noir" (black case, gold bezel) or "ivory".
include <luxor_lib.scad>

explode = 0;
ghosts  = true;
section = false;
finish  = "noir";

case_col  = finish == "noir" ? [0.07, 0.07, 0.075] : [0.93, 0.91, 0.86];
gold      = [0.83, 0.69, 0.27];
e = explode * 14;

module scene() {
    color(case_col) translate([0, 0, e * 0.4]) shell();
    if (bezel_ring) color(gold) translate([0, 0, e * 1.3]) bezel();
    color(case_col) translate([0, 0, -e * 1.2]) cover();
    if (ghosts) {
        translate([0, 0, e * 0.05]) board_ghost();
        translate([0, 0, -e * 0.6]) battery_ghost();
    }
}

if (section) difference() { scene(); translate([0, -60, -40]) cube([60, 120, 80]); }
else scene();
