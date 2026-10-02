// Luxor test-fit coupons — PRINT THESE BEFORE ANY FULL PART.
// Each coupon is cut from the real part geometry, so a pass means the full
// part will fit too.
//
//   part = "A"   front band of the shell: glass lip, buttons, top of the USB mouth
//   part = "B"   standoff pattern plate
//   part = "C"   one pair of strap lugs with the spring-bar holes
//   part = "all" A, B and C on one plate
//
// Render one:  openscad -D 'part="A"' -o coupon_A.stl coupons.scad
// Print: same material and settings as the real part; no supports.

include <luxor_lib.scad>

part = "all";

// ---------------------------------------------------------------- A
// The front 7 mm of the shell: glass lip, bezel pocket and the first part of
// the bore.  Printed face-down like the real shell (~20 min).
//
// Pass: the board drops in glass-first with light finger pressure and does
// not pass through the lip; the whole display is visible; the gold inlay
// (part 3) drops into the pocket and sits flush.  Tight? raise fit_clear
// (bore) or bezel_fit (inlay) by 0.1 and reprint.
a_back = -(watch_front_z - lip_under_z) - 4;
module coupon_a() {
    translate([0, 0, watch_front_z]) rotate([180, 0, 0])
        intersection() {
            shell();
            translate([0, 0, a_back]) cylinder(r = case_r + 0.01, h = watch_front_z - a_back + 1);
        }
}

// ---------------------------------------------------------------- B
// A thin disc the size of the bore with the three M2 clearance holes at
// standoff_xy.  Lay it on the back of the board, notch towards USB-C.
//
// Pass: an M2 screw drops into all three standoffs at once with no side
// load.  If one hole is off, measure how far and fix standoff_xy BEFORE
// the back cover is printed.
module coupon_b() {
    t = 1.6;
    difference() {
        cylinder(d = bore_d - 0.4, h = t);
        for (p = standoff_xy)
            translate([p[0], p[1], -1]) cylinder(d = m2_clear_d, h = t + 2, $fn = 32);
        translate([0, -(bore_d / 2), -1]) cylinder(d = 5, h = t + 2, $fn = 3);   // USB-C side
        translate([0, 3, t - 0.4]) linear_extrude(0.5)
            text("LUXOR B", size = 3.2, halign = "center", valign = "center");
    }
}

// ---------------------------------------------------------------- C
// The 12 o'clock lug pair with a sliver of case wall, printed as the shell
// is (face-down).
//
// Pass: your 22 mm quick-release strap's spring bar snaps in and holds; the
// strap end sits between the horns without binding.
module coupon_c() {
    translate([0, 0, watch_front_z]) rotate([180, 0, 0])
        intersection() {
            shell();
            translate([-(strap_w / 2 + lug_w + 1), case_r - 5.5, lug_back - 0.5])
                cube([strap_w + 2 * lug_w + 2, lug_len + 7, watch_front_z - lug_back + 1]);
        }
}

if (part == "A")        coupon_a();
else if (part == "B")   coupon_b();
else if (part == "C")   coupon_c();
else {
    translate([-(case_r + 3), 0, 0]) coupon_a();
    translate([ (case_r + 3), 0, 0]) coupon_b();
    translate([0, -(case_r + 16), 0]) coupon_c();
}
