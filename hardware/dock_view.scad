// Dock with the watch in place, for the render only.
include <luxor_lib.scad>
color([0.12, 0.12, 0.13]) dock();
docked() {
    color([0.07, 0.07, 0.075]) shell();
    if (bezel_ring) color([0.83, 0.69, 0.27]) bezel();
    color([0.07, 0.07, 0.075]) cover();
    color([0.03, 0.08, 0.16]) translate([0, 0, -1.2]) cylinder(d = glass_od - 0.2, h = 1.19);
}
