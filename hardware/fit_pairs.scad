// Interference pairs for check_fit.py.  Each value of `pair` renders the
// INTERSECTION of two things in the watch frame; an empty result = no clash.
include <luxor_lib.scad>
pair = "shell-cover";

// What we assume the board occupies: glass + module to the standoff tips
// everywhere, and down to board_depth except inside the landing bosses.
module board_envelope() {
    translate([0, 0, -standoff_depth]) cylinder(d = board_od, h = standoff_depth + 0.0);
    difference() {
        translate([0, 0, -board_depth]) cylinder(d = board_od, h = board_depth - standoff_depth + 0.01);
        for (p = standoff_xy) translate([p[0], p[1], -20]) cylinder(d = boss_d + 0.6, h = 40, $fn = 32);
    }
}
module glass() { translate([0, 0, -1.2]) cylinder(d = glass_od, h = 1.2); }
module cell()  { translate([-batt_l / 2, batt_cy - batt_w / 2, cover_back_z + cover_floor]) cube([batt_l, batt_w, batt_t]); }
module shell_no_nubs() { difference() { shell(); for (a = button_angles) at_angle(a) translate([bore_r - 1.5, -2, -button_depth - 2]) cube([1.6, 4, 4]); } }

if (pair == "shell-cover")  intersection() { shell(); cover(); }
if (pair == "shell-bezel")  intersection() { shell(); bezel(); }
if (pair == "cover-bezel")  intersection() { cover(); bezel(); }
if (pair == "shell-board")  intersection() { shell_no_nubs(); board_envelope(); }
if (pair == "cover-board")  intersection() { cover(); board_envelope(); }
if (pair == "shell-glass")  intersection() { shell(); glass(); }
if (pair == "cover-cell")   intersection() { cover(); cell(); }
if (pair == "board-cell")   intersection() { board_envelope(); cell(); }
if (pair == "nubs-board")   intersection() { shell(); board_envelope(); }   // expected: the nubs only
