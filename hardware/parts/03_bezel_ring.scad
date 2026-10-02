// Part 3 — gold bezel inlay (two-tone option).  Drops into the pocket in the
// shell front and sits flush; fix with a thin ring of CA glue or 0.1 mm
// double-sided tape.  Hour indices are engraved, 12 o'clock doubled.
// PRINT: flat side down as exported.  Silk gold PLA; 0.08–0.12 mm layers.
// Only needed when bezel_ring = true in luxor_params.scad.
include <../luxor_lib.scad>
translate([0, 0, -case_front_z]) difference() {
    bezel();
    // hour indices: 12 is a double tick, the rest single, engraved 0.25 mm
    for (h = [0 : 11]) rotate([0, 0, 90 - h * 30])
        for (o = (h == 0 ? [-0.7, 0.7] : [0]))
            translate([(bezel_ir + case_r - bezel_rim) / 2 - 0.8, o - 0.3, case_front_z + bezel_t - 0.25])
                cube([1.6, 0.6, 1]);
}
