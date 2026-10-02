#pragma once
/*
 * Luxor layout geometry -- 412 x 412 round display.
 *
 * This header holds every position and size the UI uses.  It deliberately has
 * no LVGL or ESP-IDF dependency so tests/test_layout.c can compile it on the
 * host and prove that:
 *
 *   - every interactive rectangle fits inside the safe circle,
 *   - the honeycomb is a true, evenly spaced hexagonal rosette,
 *   - nothing overlaps that should not.
 *
 * Coordinate system: screen pixels, origin top-left, +x = 3 o'clock,
 * +y = 6 o'clock (the USB-C side).  Physical USB-C stays at 6 o'clock.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define LUX_W          412
#define LUX_H          412
#define LUX_CX         206
#define LUX_CY         206
#define LUX_R          206

/* Anything the user must read or touch stays inside this radius.  The outer
 * ~18 px can be hidden by the printed bezel or distorted by the cover glass. */
#define LUX_SAFE_R     188
/* Purely decorative rings (watch-face bezel) may reach further out. */
#define LUX_DECOR_R    202

/* ---------------------------------------------------------------- helpers */

/* Half-width of the safe circle at a given screen row. */
static inline int lux_half_width_at(int y)
{
    int dy = y - LUX_CY;
    if (dy < 0) dy = -dy;
    if (dy >= LUX_SAFE_R) return 0;
    return (int)sqrt((double)LUX_SAFE_R * LUX_SAFE_R - (double)dy * dy);
}

/* True when all four corners of the rectangle sit inside the safe circle. */
static inline bool lux_rect_fits(int x, int y, int w, int h)
{
    const int cx[4] = { x, x + w, x, x + w };
    const int cy[4] = { y, y, y + h, y + h };
    for (int i = 0; i < 4; i++) {
        double dx = cx[i] - LUX_CX, dy = cy[i] - LUX_CY;
        if (dx * dx + dy * dy > (double)LUX_SAFE_R * LUX_SAFE_R) return false;
    }
    return true;
}

/* Same test for a circular control. */
static inline bool lux_circle_fits(int cx, int cy, int r)
{
    double dx = cx - LUX_CX, dy = cy - LUX_CY;
    return sqrt(dx * dx + dy * dy) + r <= LUX_SAFE_R;
}

/* Widest centred rectangle of height h that fits with its top edge at y. */
static inline int lux_max_centred_width(int y, int h)
{
    int a = lux_half_width_at(y);
    int b = lux_half_width_at(y + h);
    return 2 * (a < b ? a : b);
}

/* Touch targets.  Anything you can press gets at least a LUX_HIT_MIN square
 * of touch area.  A control drawn smaller keeps its look and gets an
 * invisible padded hit area of lux_hit_pad() pixels on every side (LVGL
 * "extended click area", applied in ui.c).  tests/test_layout.c checks that
 * padded areas of neighbouring controls never overlap. */
#define LUX_HIT_MIN    44

static inline int lux_hit_pad(int w, int h)
{
    int m = w < h ? w : h;
    return m >= LUX_HIT_MIN ? 0 : (LUX_HIT_MIN - m + 1) / 2;
}

/* --------------------------------------------------------- global chrome */

/* Round HOME control, identical position in every application so the gesture
 * becomes muscle memory.  Top centre keeps it clear of the keypad rows and of
 * the wrist when the watch is worn. */
#define LUX_HOME_CX    206
#define LUX_HOME_CY    58
#define LUX_HOME_R     19

/* Application title band, directly under the HOME control.  There is no
 * second line: a screen states what it is once, and the content speaks for
 * itself.  A gold hairline rule under the title does the separating that a
 * grey caption used to do, without adding words. */
#define LUX_TITLE_Y    80
#define LUX_TITLE_H    28
#define LUX_TITLE_W    228
#define LUX_TITLE_X    (LUX_CX - LUX_TITLE_W / 2)

#define LUX_RULE_Y     110
#define LUX_RULE_W     120
#define LUX_RULE_X     (LUX_CX - LUX_RULE_W / 2)

/* Shared content card used by Fitness / Music / Motion. */
#define LUX_CARD_X     76
#define LUX_CARD_Y     122
#define LUX_CARD_W     260
#define LUX_CARD_H     202

/* ------------------------------------------------------------- honeycomb */

/*
 * Flat-top hexagons: vertices at 3 and 9 o'clock, flat edges top and bottom.
 *
 * LUX_HEX_R is the *pitch* radius that sets where the tile centres go.  Two
 * flat-top hexagons of circumradius R laid out at spacing sqrt(3)*R share an
 * edge exactly -- which is what made the old rosette look like one welded
 * slab.  The tiles are therefore DRAWN at the smaller LUX_HEX_DRAW_R, leaving
 * a visible gap all the way round every tile:
 *
 *      gap = LUX_HEX_D - sqrt(3) * LUX_HEX_DRAW_R
 *          = 90 - 83.1
 *          = 6.9 px
 *
 * tests/test_layout.c asserts that gap is positive, so a future tweak to
 * either radius cannot quietly weld them back together.
 */
#define LUX_HEX_R      52                   /* pitch radius (spacing only) */
#define LUX_HEX_DRAW_R 48                   /* radius the tile is drawn at */
#define LUX_HEX_W      (2 * LUX_HEX_DRAW_R) /* 96  drawn width  */
#define LUX_HEX_H      83                   /* round(sqrt(3) * 48) drawn height */
#define LUX_HEX_D      90                   /* round(sqrt(3) * 52) centre spacing */

/* Smallest gap between the drawn edges of two neighbouring tiles, in tenths
 * of a pixel, so the test can assert on it without floating point. */
#define LUX_HEX_GAP_TENTHS (10 * LUX_HEX_D - 17 * LUX_HEX_DRAW_R)

/* Tile label box.  A flat-top hexagon narrows away from its centre line, so
 * the widest a one-line label can be is the hexagon's width at half the
 * label height, less a margin on each side.  ui.c picks the largest font for
 * which EVERY launcher name fits this width, so all seven tiles match. */
#define LUX_TILE_LABEL_H      18
#define LUX_TILE_LABEL_MARGIN 5
#define LUX_TILE_LABEL_MAX_W  74

/* Width of a drawn flat-top hexagon at vertical offset dy from its centre. */
static inline double lux_hex_width_at(double dy)
{
    if (dy < 0) dy = -dy;
    return 2.0 * (LUX_HEX_DRAW_R - dy / 1.7320508075688772);
}

/* Seven launcher slots: index 0 is the centre, 1..6 run clockwise from 12. */
#define LUX_TILES 7

typedef struct {
    int16_t cx, cy;
} lux_tile_pos_t;

/*  slot 0  centre      DISPLAY
 *  slot 1  12 o'clock  SETTINGS
 *  slot 2   2 o'clock  MUSIC
 *  slot 3   4 o'clock  CALCULATOR
 *  slot 4   6 o'clock  CLOCK
 *  slot 5   8 o'clock  TILT MAZE
 *  slot 6  10 o'clock  FITNESS
 *
 * Offsets are exactly (0, -D), (+3R/2, -D/2), (+3R/2, +D/2), (0, +D),
 * (-3R/2, +D/2), (-3R/2, -D/2) -- a mathematically even rosette, not
 * hand-placed pixels.
 */
static const lux_tile_pos_t lux_tiles[LUX_TILES] = {
    { LUX_CX,                          LUX_CY                     },
    { LUX_CX,                          LUX_CY - LUX_HEX_D         },
    { LUX_CX + (3 * LUX_HEX_R) / 2,    LUX_CY - LUX_HEX_D / 2     },
    { LUX_CX + (3 * LUX_HEX_R) / 2,    LUX_CY + LUX_HEX_D / 2     },
    { LUX_CX,                          LUX_CY + LUX_HEX_D         },
    { LUX_CX - (3 * LUX_HEX_R) / 2,    LUX_CY + LUX_HEX_D / 2     },
    { LUX_CX - (3 * LUX_HEX_R) / 2,    LUX_CY - LUX_HEX_D / 2     },
};

/* Time above the rosette, date and battery below it.  Both are centred on the
 * same vertical axis as the rosette so the whole screen reads as one column;
 * the old strip sat 9 px high and looked bolted on. */
#define LUX_HOME_TIME_W  156
#define LUX_HOME_TIME_H  30
#define LUX_HOME_TIME_X  (LUX_CX - LUX_HOME_TIME_W / 2)
#define LUX_HOME_TIME_Y  38

#define LUX_HOME_FOOT_W  170
#define LUX_HOME_FOOT_H  22
#define LUX_HOME_FOOT_X  (LUX_CX - LUX_HOME_FOOT_W / 2)
#define LUX_HOME_FOOT_Y  346

/* Battery badge, top right of home: "82%" then a small battery glyph.
 * It sits in the gap between the clock strip, the 12 o'clock tile and the
 * 2 o'clock tile, and its outer corner stays inside LUX_SAFE_R.  It is not a
 * control, so taps on it still count as empty background. */
#define LUX_BATT_X        266
#define LUX_BATT_Y        78
#define LUX_BATT_W        68
#define LUX_BATT_H        22

#define LUX_BATT_TEXT_W   40   /* "100%" is 38 px in Inter 14, right-aligned */
#define LUX_BATT_GAP      3
#define LUX_BATT_BODY_W   22
#define LUX_BATT_BODY_H   12
#define LUX_BATT_NUB_W    2
#define LUX_BATT_NUB_H    6
#define LUX_BATT_STROKE   1
/* Fill area inside the outline, with 1 px of air on each side. */
#define LUX_BATT_INNER_W  (LUX_BATT_BODY_W - 2 * (LUX_BATT_STROKE + 1))
#define LUX_BATT_INNER_H  (LUX_BATT_BODY_H - 2 * (LUX_BATT_STROKE + 1))

/* ----------------------------------------------------------- calculator */

#define LUX_CALC_DISP_X  86
#define LUX_CALC_DISP_Y  88
#define LUX_CALC_DISP_W  240
#define LUX_CALC_DISP_H  50

#define LUX_CALC_KEY_W   56
#define LUX_CALC_KEY_H   40
#define LUX_CALC_GAP_X   8
#define LUX_CALC_GAP_Y   4
#define LUX_CALC_ROW0_Y  148
#define LUX_CALC_ROWS    5

/* Row 5 sits low enough that only three keys fit inside the circle. */
static inline int lux_calc_cols(int row) { return row >= 4 ? 3 : 4; }

static inline int lux_calc_row_y(int row)
{
    return LUX_CALC_ROW0_Y + row * (LUX_CALC_KEY_H + LUX_CALC_GAP_Y);
}

static inline int lux_calc_key_x(int row, int col)
{
    int n = lux_calc_cols(row);
    int span = n * LUX_CALC_KEY_W + (n - 1) * LUX_CALC_GAP_X;
    return LUX_CX - span / 2 + col * (LUX_CALC_KEY_W + LUX_CALC_GAP_X);
}

/* -------------------------------------------------------------- settings */

/* Scrollable column.  Rows are fixed height so scroll snapping lands cleanly. */
#define LUX_LIST_X       82
#define LUX_LIST_Y       116
#define LUX_LIST_W       248
#define LUX_LIST_H       228
#define LUX_ROW_H        54

/* Controls inside a list row, in row-local pixels.  Steppers: two buttons
 * at the right edge.  The gap leaves room for both padded touch areas. */
#define LUX_STEP_BTN_W   38
#define LUX_STEP_BTN_H   36
#define LUX_STEP_BTN_GAP 10
#define LUX_STEP_BTN_Y   9
#define LUX_ROW_INNER_W  (LUX_LIST_W - 8)
static inline int lux_step_btn_x(int i)   /* 0 = minus, 1 = plus */
{
    return LUX_ROW_INNER_W - 10 - LUX_STEP_BTN_W - (1 - i) * (LUX_STEP_BTN_W + LUX_STEP_BTN_GAP);
}
#define LUX_SWITCH_W     54
#define LUX_SWITCH_H     28
#define LUX_SWITCH_X     (LUX_LIST_W - 74)
#define LUX_SWITCH_Y     13
#define LUX_SLIDER_H     12

/* ------------------------------------------------------------ tilt maze */

/* The arena is a circle, but no longer the whole glass: the header sits
 * above it and the two controls sit below it, so nothing is drawn on top of
 * anything else.  A smaller arena also makes the maze harder in a good way,
 * because the ball covers a larger fraction of each passage. */
#define LUX_MAZE_CX      LUX_CX
#define LUX_MAZE_CY      232
#define LUX_MAZE_R       108          /* arena inner radius */
#define LUX_BALL_R       9
#define LUX_GOAL_R       14

typedef struct {
    int16_t x, y, w, h;               /* top-left + size, screen pixels */
} lux_wall_t;

/* Five walls.  The only open route is down the left-hand side, under the
 * long vertical, and back in above the bottom bar; the right-hand side is a
 * blind alley that looks like it should work.  Every wall is inside the
 * arena and every passage is wider than the ball -- test_layout.c checks
 * both. */
#define LUX_MAZE_WALLS 5
static const lux_wall_t lux_maze_walls[LUX_MAZE_WALLS] = {
    { 150, 176, 100,   8 },           /* lintel under the start        */
    { 160, 176,   8,  84 },           /* long left vertical            */
    { 160, 252,  76,   8 },           /* shelf                         */
    { 248, 200,   8,  76 },           /* right vertical, short         */
    { 170, 300,  60,   8 },           /* bottom bar                    */
};

/* Ball start and goal, in screen pixels. */
#define LUX_MAZE_START_X LUX_MAZE_CX
#define LUX_MAZE_START_Y (LUX_MAZE_CY - 80)
#define LUX_MAZE_GOAL_X  LUX_MAZE_CX
#define LUX_MAZE_GOAL_Y  (LUX_MAZE_CY + 46)

/* Reset and flip controls under the arena. */
#define LUX_MAZE_BTN_R   17
#define LUX_MAZE_BTN_CY  364
#define LUX_MAZE_BTN_DX  34

/* ---------------------------------------------------------------- clock */

/* A dress-watch dial: gold bezel, ivory face, black Roman numerals, gold
 * hands.  Three concentric rings give it depth without a texture bitmap. */
#define LUX_BEZEL_D      404          /* outermost gold ring, decorative   */
#define LUX_DIAL_D       364          /* lv_meter size; radius 182         */
#define LUX_DIAL_X       ((LUX_W - LUX_DIAL_D) / 2)
#define LUX_DIAL_Y       ((LUX_H - LUX_DIAL_D) / 2)
#define LUX_CHAPTER_D    336          /* hairline ring just inside the ticks */
#define LUX_INNER_D      212          /* hairline ring around the wordmark  */

/* Hand lengths are r_mod offsets from the dial radius (182).
 * Each hand is two overlapping needles -- a wide stub and a narrow tip --
 * which reads as a tapered hand without drawing a polygon every frame. */
#define LUX_HAND_HOUR_MOD    (-92)    /* tip  at r = 90  */
#define LUX_HAND_HOUR_STUB   (-128)   /* stub to r = 54  */
#define LUX_HAND_MIN_MOD     (-54)    /* tip  at r = 128 */
#define LUX_HAND_MIN_STUB    (-112)   /* stub to r = 70  */
#define LUX_HAND_SEC_MOD     (-30)    /* tip  at r = 152 */
#define LUX_HAND_CAP_R   11           /* gold centre cap */

/* Twelve Roman numerals on an exact radius. */
#define LUX_NUMERAL_R    139
#define LUX_NUMERAL_W    54
#define LUX_NUMERAL_H    24

/* Date window at 3 o'clock, wordmark above centre, digital line below. */
#define LUX_DATE_W       44
#define LUX_DATE_H       28
#define LUX_DATE_X       (LUX_CX + 60)
#define LUX_DATE_Y       (LUX_CY - LUX_DATE_H / 2)
#define LUX_MARK_DY      (-62)        /* "LUXOR" wordmark, above centre */
#define LUX_DIGITAL_DY   66           /* digital time, below centre     */

/* ------------------------------------------------------- generic rows */

/* Left edge of a centred row of n equal buttons. */
static inline int lux_row_x(int n, int w, int gap)
{
    return LUX_CX - (n * w + (n - 1) * gap) / 2;
}
static inline int lux_row_item_x(int i, int n, int w, int gap)
{
    return lux_row_x(n, w, gap) + i * (w + gap);
}

/* ------------------------------------------------------------- music */

#define LUX_DISC_D       148
#define LUX_DISC_CY      198
/* Level meter inside the disc: LUX_EQ_BARS rounded bars, centred. */
#define LUX_EQ_BARS      5
#define LUX_EQ_W         10
#define LUX_EQ_GAP       8
#define LUX_EQ_MAX_H     64
#define LUX_EQ_MIN_H     10
static inline int lux_eq_x(int i)   /* left edge, screen coordinates */
{
    int span = LUX_EQ_BARS * LUX_EQ_W + (LUX_EQ_BARS - 1) * LUX_EQ_GAP;
    return LUX_CX - span / 2 + i * (LUX_EQ_W + LUX_EQ_GAP);
}
#define LUX_MUSIC_TITLE_Y 274
#define LUX_MUSIC_BTN_W  72
#define LUX_MUSIC_BTN_H  44
#define LUX_MUSIC_BTN_GAP 10
#define LUX_MUSIC_BTN_Y  302
#define LUX_MUSIC_FOOT_Y 356
#define LUX_MUSIC_FOOT_H 18
#define LUX_MUSIC_FOOT_W 150

/* ------------------------------------------------- motion / IMU setup */

#define LUX_MOTION_STAT_Y 114        /* raw readout, under the rule   */
#define LUX_MOTION_STAT_H 18
#define LUX_MOTION_STAT_W 220
#define LUX_LEVEL_CY     216          /* bubble-level centre */
#define LUX_LEVEL_R      78
#define LUX_LEVEL_DOT_R  11
#define LUX_AXIS_BTN_W   76
#define LUX_AXIS_BTN_H   40
#define LUX_AXIS_BTN_GAP 8
#define LUX_AXIS_BTN_Y   300
#define LUX_ZERO_BTN_W   136
#define LUX_ZERO_BTN_H   32
#define LUX_ZERO_BTN_Y   348

/* ---------------------------------------------------- activity rings
 *
 * STEPS shows three concentric rings in the manner of a luxury chronograph's
 * sub-dials: steps against the goal (outer), active minutes against
 * LUX_ACTIVE_GOAL_MIN (middle) and distance against the goal distance
 * (inner).  The numbers sit in a three-column row under the rings, never on
 * top of an arc. */
#define LUX_ACT_CY          196
#define LUX_ACT_RING_W      14
#define LUX_ACT_RING_GAP    3
#define LUX_ACT_D0          156
#define LUX_ACT_RINGS       3
#define LUX_ACTIVE_GOAL_MIN 30

static inline int lux_act_ring_d(int i)
{
    return LUX_ACT_D0 - 2 * i * (LUX_ACT_RING_W + LUX_ACT_RING_GAP);
}

#define LUX_ACT_STAT_Y      288
#define LUX_ACT_STAT_H      44
#define LUX_ACT_STAT_W      84
#define LUX_ACT_STAT_PITCH  88
#define LUX_ACT_VAL_H       24

static inline int lux_act_stat_x(int col) /* left edge of column 0..2 */
{
    return LUX_CX + (col - 1) * LUX_ACT_STAT_PITCH - LUX_ACT_STAT_W / 2;
}

/* ------------------------------------------------- display / gallery */

/* The Display app exists to show artwork, so the artwork is the biggest
 * thing on the screen and everything else is caption-sized.  The plate is a
 * square canvas clipped to a circle, centred slightly high to leave room for
 * the title and the page dots. */
#define LUX_ART_D        252          /* canvas edge, square             */
#define LUX_ART_FRAME    5            /* gold mount around the plate     */
#define LUX_ART_CY       212
#define LUX_ART_X        (LUX_CX - LUX_ART_D / 2)
#define LUX_ART_Y        (LUX_ART_CY - LUX_ART_D / 2)
#define LUX_ART_COUNT    6

/* The plate's name is its only caption; there is no app title on this
 * screen because the picture is the point. */
#define LUX_ART_NAME_Y   348
#define LUX_ART_NAME_H   22
#define LUX_ART_NAME_W   160

#define LUX_DOT_D        9
#define LUX_DOT_GAP      9
#define LUX_DOT_Y        376
#define LUX_ARROW_R      22
#define LUX_ARROW_CY     LUX_ART_CY
#define LUX_ARROW_DX     156

/* Theme preview swatch, now a row inside Settings rather than a whole app. */
#define LUX_SWATCH_D     28
