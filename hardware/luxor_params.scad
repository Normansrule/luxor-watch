// Luxor case — shared parameters.  Every part file includes luxor_lib.scad,
// which includes this.  Units: millimetres.
//
// WATCH FRAME (used by every part and by assembly.scad):
//   origin   centre of the cover-glass FRONT face
//   +X       3 o'clock        +Y   12 o'clock      +Z   out of the glass
//   USB-C at 6 o'clock (-Y).  Everything behind the glass has negative Z.
//
// Status tag on every value:
//   FIXED    decided (brief section 11) — change only on purpose
//   DRAWING  from the Waveshare structural drawing (as recorded in ORBIT B4)
//   ASSUMED  carried from the ORBIT B-series cases; NOT verified on the board
//   MEASURE  must come from calipers or the Waveshare STEP in Onshape
//   DESIGN   a free styling / fit choice
//
// Print coupons.scad and pass every check before printing a full part.

// ================================================================ case
case_od        = 50.2;   // FIXED   outer diameter
shell_wall     = 2.0;    // FIXED   uniform wall
bore_d         = 46.2;   // FIXED   = case_od - 2 * shell_wall
fit_clear      = 0.0;    // DESIGN  add 0.1..0.3 if coupon A is tight on your printer

// ======================================================== board + glass
board_od       = 45.8;   // ASSUMED widest diameter of board + cover glass
glass_od       = 45.8;   // MEASURE cover-glass diameter (lip must overlap it)
active_d       = 37.0;   // DRAWING active display 36.96 mm
board_depth    = 10.65;  // DRAWING glass face -> deepest part on the back
standoff_depth = 9.33;   // ASSUMED glass face -> tips of the brass standoffs
depth_clear    = 0.35;   // DESIGN  air between the deepest part and anything printed

// Three M2 brass standoffs.  Pitches are DRAWING (lower pair 23.54 apart,
// top-to-lower 32.70); the absolute origin is ASSUMED.
standoff_xy    = [ [  0.00,  18.00 ],
                   [ 11.77, -14.70 ],
                   [-11.77, -14.70 ] ];

// ================================================== front bezel + glass lip
lip_t          = 0.8;    // DESIGN  bezel thickness in front of the glass
lip_id         = 41.0;   // DESIGN  viewing opening (active_d + 2 .. glass_od - 2)
lip_chamfer    = 0.8;    // DESIGN  45° bevel on the opening edge
gasket_t       = 0.30;   // DESIGN  foam/rubber gasket between lip and glass (BOM 7)
gasket_squeeze = 0.10;   // DESIGN  how much the gasket is compressed when closed

// Optional two-tone bezel: a gold ring (print in silk gold) inlaid in a
// pocket in the shell front, flush with it, framed by a fine black ring
// inside and a hairline black rim outside — like a ceramic bezel insert.
// The front stays flat, so the shell prints face-down with no overhang.
bezel_ring     = true;   // DESIGN  false = plain front with a rounded edge
bezel_t        = 0.8;    // DESIGN  inlay thickness (pocket depth)
bezel_ir       = lip_id / 2 + 1.0;  // DESIGN inner radius of the pocket
bezel_rim      = 0.6;    // DESIGN  black rim outside the inlay
bezel_fit      = 0.12;   // DESIGN  clearance on each diameter (0.1 = press fit)

// ================================================================ split
// The shell runs from the bezel back to the split plane; the back cover
// runs from there to the wrist.  The plane sits just behind the deepest
// board part, so nothing printed touches the PCB.
z_split        = -(board_depth + depth_clear);          // -11.0

// ================================================================ battery
// Single-cell LiPo sized for the pocket.  402530 = 4.0 x 25 x 30 mm, about
// 250 mAh.  CONNECTOR: Waveshare lists MX1.25 2-pin — verify polarity.
batt_l         = 30.5;   // MEASURE along X (with tape)
batt_w         = 25.5;   // MEASURE along Y
batt_t         = 4.2;    // MEASURE thickness incl. wrap
batt_cy        = 1.65;   // DESIGN  centred between the top and lower standoff bosses
batt_rail_h    = 1.6;    // DESIGN  end-rail height (the bosses locate the cell along Y)
batt_rail_t    = 0.8;

// ============================================================= back cover
cover_floor    = 1.2;    // DESIGN
back_chamfer   = 1.4;    // DESIGN  45° edge at the wrist (prints without support)
boss_d         = 6.0;    // DESIGN  standoff landing boss
m2_clear_d     = 2.4;    // DESIGN  M2 clearance
m2_head_d      = 4.2;    // DESIGN  counterbore for M2 pan / socket head
m2_head_h      = 3.2;    // DESIGN  counterbore depth, chosen so a standard M2x6 lands ~2 mm of thread
m2_engage      = 2.0;    // MEASURE usable thread depth in the standoffs (must be less than it)
back_text      = "LUXOR";          // DESIGN  engraved on the case back
back_subtext   = "HANDBUILT  ·  No. 001";   // keep it short: it sits inside the 28 mm ring
engrave_d      = 0.4;    // DESIGN

// Rotation key: a block on the cover rim at 12 o'clock that sits in a notch
// in the shell rim, so the USB-C mouth and buttons always line up.
key_w          = 3.0;
key_h          = 1.0;    // how far it rises into the shell
key_depth      = 1.0;    // radial depth of the notch into the 2 mm wall

// ================================================================ USB-C
usb_depth      = 9.0;    // ASSUMED receptacle centre behind the glass face
cable_w        = 12.5;   // MEASURE your cable's overmould width
cable_h        = 7.0;    // MEASURE your cable's overmould height
mouth_clear    = 0.6;    // DESIGN  added all round

// ============================================================== buttons
// PWR and BOOT, pressed through seamless flexure tabs cut into the wall.
// Angles are measured from 3 o'clock, counter-clockwise, front view.
button_angles  = [ 35, -35 ];   // ASSUMED (photo-derived in ORBIT B4): ~2 and ~4 o'clock
button_depth   = 7.5;    // ASSUMED switch centre behind the glass face
button_style   = "flexure";     // "flexure" | "none"
flex_w         = 5.0;    // DESIGN  tab width (along the wall)
flex_len       = 6.0;    // DESIGN  tab length (along Z), hinged at the front end
flex_slot      = 0.6;    // DESIGN  cut width around the tab
flex_t         = 1.2;    // DESIGN  tab thickness (wall 2.0 is thinned from inside)
flex_nub_d     = 2.2;    // DESIGN
flex_reach     = 0.35;   // ASSUMED nub protrusion past the bore surface onto the switch

// ============================================================= speaker/mic
speaker_angle  = 180;    // ASSUMED speaker slots at 9 o'clock on the back cover
speaker_slots  = 4;
mic_angle      = -140;   // ASSUMED mic hole ~8 o'clock on the shell
mic_depth      = 7.5;    // ASSUMED
mic_d          = 1.2;

// ============================================================== strap lugs
strap_w        = 22.0;   // DESIGN  22 mm quick-release strap (BOM 5)
lug_w          = 2.4;    // DESIGN  each lug horn
lug_len        = 7.0;    // DESIGN  beyond the case radius
lug_root_top   = 0.0;    // DESIGN  lug front face height at the case (Z)
lug_back       = -10.6;  // DESIGN  lug rear face (stays just in front of z_split)
bar_y_from_edge= 4.6;    // DESIGN  spring-bar axis beyond the case radius
bar_z          = -7.4;   // DESIGN
bar_hole_d     = 1.1;    // DESIGN  for 1.5 mm spring bars with ~0.8 mm tips; drill to clean
bar_hole_depth = 1.3;    // DESIGN  blind, from the inner face of each horn

// ============================================================== derived
lip_under_z    = gasket_t - gasket_squeeze;              // lip underside
case_front_z   = lip_under_z + lip_t;                    // shell front face
watch_front_z  = case_front_z + (bezel_ring ? bezel_t : 0); // front of the watch
cover_back_z   = -(board_depth + depth_clear + batt_t + 0.2 + cover_floor); // wrist face
case_r         = case_od / 2;
bore_r         = bore_d / 2;
screw_len      = (-standoff_depth) - cover_back_z - m2_head_h + m2_engage;      // under-head
watch_thick    = watch_front_z - cover_back_z;

$fn            = 144;

// ======================================================= sanity checks
// A wrong number stops the render instead of printing a useless part.
assert(abs(bore_d - (case_od - 2 * shell_wall)) < 0.01, "bore_d must equal case_od - 2*shell_wall");
assert(board_od <= bore_d, "board does not fit the bore");
assert(lip_id >= active_d + 2, "lip would hide the edge of the display");
assert(lip_id <= glass_od - 2, "lip too wide: the glass could fall through");
assert(!bezel_ring || case_r - bezel_rim - bezel_ir >= 2.5, "bezel inlay would be too narrow to print");
assert(lug_back > z_split, "lugs must end in front of the split plane");
assert(batt_l < bore_d - 4 && batt_w < bore_d - 12, "battery does not fit the pocket");
for (p = standoff_xy) {
    assert(norm(p) + boss_d / 2 < bore_r - 0.5, "a standoff boss sits outside the bore");
    // bosses must clear the cell by at least 0.2 mm
    assert(abs(p[0]) > batt_l / 2 + boss_d / 2 + 0.2
           || abs(p[1] - batt_cy) - batt_w / 2 - boss_d / 2 >= 0.2,
           "a standoff boss collides with the battery");
}
assert(m2_engage < 4.0, "check the standoff thread depth before trusting m2_engage");

echo(str("LUXOR: watch thickness ", watch_thick, " mm; M2 under-head length for ",
         m2_engage, " mm of thread = ", screw_len, " mm (use M2x", round(screw_len), ")"));
