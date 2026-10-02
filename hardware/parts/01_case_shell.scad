// Part 1 — case shell (glass lip, black inner bezel, integral lugs,
// flexure buttons, USB-C mouth).
// PRINT: face-down as exported (the front is on the bed).  No supports.
// Black or white PLA/PETG; 0.12–0.16 mm layers; 4 walls; 40 % infill.
include <../luxor_lib.scad>
translate([0, 0, watch_front_z]) rotate([180, 0, 0]) shell();
