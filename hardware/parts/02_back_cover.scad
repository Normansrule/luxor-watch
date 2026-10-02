// Part 2 — back cover (standoff bosses, battery cradle, speaker slots, engraving).
// PRINT: back-down as exported (engraving on the bed).  No supports.
// Same colour as the shell; 0.16 mm layers; 4 walls; 30 % infill.
include <../luxor_lib.scad>
translate([0, 0, -cover_back_z]) cover();
