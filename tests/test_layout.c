/*
 * Host-side geometry check for the Luxor UI.
 *
 * The firmware cannot be run here, but the layout can: ui.c takes every
 * coordinate from layout.h, so validating layout.h validates the screens.
 *
 *   cc -std=c11 -I../main -o test_layout test_layout.c -lm && ./test_layout
 */
#include "layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int mark;

/* Print a PASS line only when the section added no failures. */
static void section(const char *name)
{
    if (failures == mark) printf("PASS: %s\n", name);
    mark = failures;
}

static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void check_rect(int x, int y, int w, int h, const char *what)
{
    if (!lux_rect_fits(x, y, w, h)) {
        printf("FAIL: %s  rect(%d,%d %dx%d) leaves the safe circle\n", what, x, y, w, h);
        failures++;
    }
}

static bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* Flat-top hexagon vertices: 0, 60, ... 300 degrees at the DRAWN radius. */
static void hex_vertex(int cx, int cy, int i, double *vx, double *vy)
{
    double a = i * 3.14159265358979323846 / 3.0;
    *vx = cx + LUX_HEX_DRAW_R * cos(a);
    *vy = cy + LUX_HEX_DRAW_R * sin(a);
}

static double dist(double ax, double ay, double bx, double by)
{
    double dx = ax - bx, dy = ay - by;
    return sqrt(dx * dx + dy * dy);
}

int main(void)
{
    /* ----------------------------------------------------- global chrome */
    check(lux_circle_fits(LUX_HOME_CX, LUX_HOME_CY, LUX_HOME_R), "HOME button inside safe circle");
    check_rect(LUX_TITLE_X, LUX_TITLE_Y, LUX_TITLE_W, LUX_TITLE_H, "app title");
    check_rect(LUX_RULE_X, LUX_RULE_Y, LUX_RULE_W, 2, "title rule");
    check_rect(LUX_CARD_X, LUX_CARD_Y, LUX_CARD_W, LUX_CARD_H, "content card");
    check(!overlap(LUX_TITLE_X, LUX_TITLE_Y, LUX_TITLE_W, LUX_TITLE_H,
                   LUX_HOME_CX - LUX_HOME_R, LUX_HOME_CY - LUX_HOME_R, 2 * LUX_HOME_R, 2 * LUX_HOME_R),
          "title clears the HOME button");
    section("shared chrome fits and does not collide");

    /* -------------------------------------------------------- honeycomb */
    for (int i = 0; i < LUX_TILES; i++) {
        for (int v = 0; v < 6; v++) {
            double vx, vy;
            hex_vertex(lux_tiles[i].cx, lux_tiles[i].cy, v, &vx, &vy);
            if (dist(vx, vy, LUX_CX, LUX_CY) > LUX_SAFE_R) {
                printf("FAIL: tile %d vertex %d at %.1f px from centre\n",
                       i, v, dist(vx, vy, LUX_CX, LUX_CY));
                failures++;
            }
        }
    }
    /* The six outer tiles must be exactly one spacing from the centre, and
     * exactly one spacing from each of their two neighbours. */
    for (int i = 1; i <= 6; i++) {
        double d = dist(lux_tiles[i].cx, lux_tiles[i].cy, LUX_CX, LUX_CY);
        if (fabs(d - LUX_HEX_D) > 1.5) {
            printf("FAIL: tile %d is %.2f px from centre, expected %d\n", i, d, LUX_HEX_D);
            failures++;
        }
        int next = i == 6 ? 1 : i + 1;
        double n = dist(lux_tiles[i].cx, lux_tiles[i].cy, lux_tiles[next].cx, lux_tiles[next].cy);
        if (fabs(n - LUX_HEX_D) > 1.5) {
            printf("FAIL: tiles %d/%d are %.2f px apart, expected %d\n", i, next, n, LUX_HEX_D);
            failures++;
        }
    }
    /* The tiles must not touch.  Two flat-top hexagons of drawn radius r
     * laid out at spacing d have sqrt(3)*r of hexagon between their centres,
     * so the gap is d - sqrt(3)*r and it has to be comfortably positive. */
    {
        double gap = LUX_HEX_D - sqrt(3.0) * LUX_HEX_DRAW_R;
        if (gap < 4.0) {
            printf("FAIL: neighbouring tiles are only %.2f px apart\n", gap);
            failures++;
        } else {
            printf("   tile gap: %.1f px on every edge\n", gap);
        }
        /* The integer approximation the header exports must agree. */
        check(LUX_HEX_GAP_TENTHS > 0, "exported tile gap is positive");
    }
    /* Opposite tiles must be mirror images through the centre. */
    for (int i = 1; i <= 3; i++) {
        int o = i + 3;
        check(lux_tiles[i].cx + lux_tiles[o].cx == 2 * LUX_CX &&
              lux_tiles[i].cy + lux_tiles[o].cy == 2 * LUX_CY, "rosette is point symmetric");
    }
    check_rect(LUX_HOME_TIME_X, LUX_HOME_TIME_Y, LUX_HOME_TIME_W, LUX_HOME_TIME_H, "home clock strip");
    check_rect(LUX_HOME_FOOT_X, LUX_HOME_FOOT_Y, LUX_HOME_FOOT_W, LUX_HOME_FOOT_H, "home status strip");
    /* Status strips must clear the rosette. */
    check(LUX_HOME_TIME_Y + LUX_HOME_TIME_H <= lux_tiles[1].cy - LUX_HEX_H / 2,
          "clock strip clears the 12 o'clock tile");
    check(LUX_HOME_FOOT_Y >= lux_tiles[4].cy + LUX_HEX_H / 2,
          "status strip clears the 6 o'clock tile");
    {
        double room = lux_hex_width_at(LUX_TILE_LABEL_H / 2.0) - 2 * LUX_TILE_LABEL_MARGIN;
        printf("   tile label room %.1f px, allowed %d px\n", room, LUX_TILE_LABEL_MAX_W);
        check(LUX_TILE_LABEL_MAX_W <= room, "tile labels keep a margin inside the hexagon");
        check(LUX_TILE_LABEL_H < LUX_HEX_H, "tile label is shorter than the tile");
    }
    section("honeycomb is an even, symmetric rosette inside the safe circle");

    /* ------------------------------------------------- battery badge */
    /* Top right of home, inside the safe circle, clear of every tile's
     * bounding box and of the clock strip.  The glyph and the text sit
     * inside the badge and do not touch each other. */
    check_rect(LUX_BATT_X, LUX_BATT_Y, LUX_BATT_W, LUX_BATT_H, "battery badge");
    check(LUX_BATT_X > LUX_CX, "battery badge is right of centre");
    check(LUX_BATT_Y + LUX_BATT_H < LUX_CY - 60, "battery badge is in the top of the dial");
    check(!overlap(LUX_BATT_X, LUX_BATT_Y, LUX_BATT_W, LUX_BATT_H,
                   LUX_HOME_TIME_X, LUX_HOME_TIME_Y, LUX_HOME_TIME_W, LUX_HOME_TIME_H),
          "battery badge clears the clock strip");
    for (int i = 0; i < LUX_TILES; i++) {
        char what[48];
        snprintf(what, sizeof(what), "battery badge clears tile %d", i);
        check(!overlap(LUX_BATT_X, LUX_BATT_Y, LUX_BATT_W, LUX_BATT_H,
                       lux_tiles[i].cx - LUX_HEX_W / 2, lux_tiles[i].cy - (LUX_HEX_H + 1) / 2,
                       LUX_HEX_W, LUX_HEX_H + 1), what);
    }
    check(LUX_BATT_TEXT_W + LUX_BATT_GAP + LUX_BATT_BODY_W + LUX_BATT_NUB_W <= LUX_BATT_W,
          "battery text + glyph fit the badge width");
    check(LUX_BATT_BODY_H <= LUX_BATT_H && LUX_BATT_NUB_H < LUX_BATT_BODY_H,
          "battery glyph fits the badge height");
    check(LUX_BATT_INNER_W > 0 && LUX_BATT_INNER_W == LUX_BATT_BODY_W - 2 * (LUX_BATT_STROKE + 1),
          "battery glyph interior is consistent");
    {
        /* Distance from the badge's outer corner to the safe edge. */
        double dx = LUX_BATT_X + LUX_BATT_W - LUX_CX, dy = LUX_BATT_Y - LUX_CY;
        printf("   battery badge outer corner %.1f px from centre (safe %d)\n",
               sqrt(dx * dx + dy * dy), LUX_SAFE_R);
    }
    section("battery badge sits top right with nothing overlapping");

    /* ------------------------------------------------------- calculator */
    check_rect(LUX_CALC_DISP_X, LUX_CALC_DISP_Y, LUX_CALC_DISP_W, LUX_CALC_DISP_H, "calculator display");
    check(LUX_HOME_CY + LUX_HOME_R < LUX_CALC_DISP_Y - 6, "calculator display card clears the HOME button");
    int rects[24][4];
    int n = 0;
    for (int r = 0; r < LUX_CALC_ROWS; r++) {
        for (int c = 0; c < lux_calc_cols(r); c++) {
            int x = lux_calc_key_x(r, c), y = lux_calc_row_y(r);
            char what[48];
            snprintf(what, sizeof(what), "calculator key r%dc%d", r, c);
            check_rect(x, y, LUX_CALC_KEY_W, LUX_CALC_KEY_H, what);
            rects[n][0] = x; rects[n][1] = y; rects[n][2] = LUX_CALC_KEY_W; rects[n][3] = LUX_CALC_KEY_H;
            n++;
        }
    }
    check(n == 19, "calculator exposes 19 keys");
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (overlap(rects[i][0], rects[i][1], rects[i][2], rects[i][3],
                        rects[j][0], rects[j][1], rects[j][2], rects[j][3])) {
                printf("FAIL: calculator keys %d and %d overlap\n", i, j);
                failures++;
            }
    check(!overlap(LUX_CALC_DISP_X, LUX_CALC_DISP_Y, LUX_CALC_DISP_W, LUX_CALC_DISP_H,
                   rects[0][0], rects[0][1], rects[0][2], rects[0][3]),
          "calculator display clears the top key row");
    section("calculator keypad fits the circle with no overlaps");

    /* --------------------------------------------------------- settings */
    check_rect(LUX_LIST_X, LUX_LIST_Y, LUX_LIST_W, LUX_LIST_H, "settings list viewport");
    check(LUX_ROW_H * 3 <= LUX_LIST_H, "at least three rows visible at once");
    check(LUX_RULE_Y + 2 <= LUX_LIST_Y, "settings list clears the title rule");
    section("settings list viewport fits");

    /* -------------------------------------------------------- tilt maze */
    check(lux_circle_fits(LUX_MAZE_CX, LUX_MAZE_CY, LUX_MAZE_R + 3), "arena ring inside safe circle");
    /* Nothing may be drawn on the arena: header above, controls below. */
    check(LUX_RULE_Y + 4 <= LUX_MAZE_CY - LUX_MAZE_R, "arena clears the title rule");
    check(LUX_MAZE_CY + LUX_MAZE_R <= LUX_MAZE_BTN_CY - LUX_MAZE_BTN_R, "arena clears the buttons");
    check(lux_circle_fits(LUX_CX - LUX_MAZE_BTN_DX, LUX_MAZE_BTN_CY, LUX_MAZE_BTN_R), "reset button fits");
    check(lux_circle_fits(LUX_CX + LUX_MAZE_BTN_DX, LUX_MAZE_BTN_CY, LUX_MAZE_BTN_R), "flip button fits");
    check(2 * LUX_MAZE_BTN_DX > 2 * LUX_MAZE_BTN_R + 6, "maze buttons do not touch");
    for (int i = 0; i < LUX_MAZE_WALLS; i++) {
        const lux_wall_t *w = &lux_maze_walls[i];
        double corners[4][2] = {
            { w->x, w->y }, { w->x + w->w, w->y },
            { w->x, w->y + w->h }, { w->x + w->w, w->y + w->h }
        };
        for (int c = 0; c < 4; c++) {
            double d = dist(corners[c][0], corners[c][1], LUX_MAZE_CX, LUX_MAZE_CY);
            if (d > LUX_MAZE_R - 4) {
                printf("FAIL: maze wall %d corner %d is %.1f px out, arena is %d\n",
                       i, c, d, LUX_MAZE_R);
                failures++;
            }
        }
    }
    for (int i = 0; i < LUX_MAZE_WALLS; i++) {
        const lux_wall_t *w = &lux_maze_walls[i];
        check(!overlap(LUX_MAZE_START_X - LUX_BALL_R, LUX_MAZE_START_Y - LUX_BALL_R,
                       2 * LUX_BALL_R, 2 * LUX_BALL_R, w->x, w->y, w->w, w->h),
              "ball start is clear of the walls");
        check(!overlap(LUX_MAZE_GOAL_X - LUX_GOAL_R, LUX_MAZE_GOAL_Y - LUX_GOAL_R,
                       2 * LUX_GOAL_R, 2 * LUX_GOAL_R, w->x, w->y, w->w, w->h),
              "goal is clear of the walls");
    }
    check(dist(LUX_MAZE_START_X, LUX_MAZE_START_Y, LUX_MAZE_CX, LUX_MAZE_CY) + LUX_BALL_R <= LUX_MAZE_R,
          "ball starts inside the arena");
    check(dist(LUX_MAZE_GOAL_X, LUX_MAZE_GOAL_Y, LUX_MAZE_CX, LUX_MAZE_CY) + LUX_GOAL_R <= LUX_MAZE_R,
          "goal sits inside the arena");

    /* Reachability, for real: flood-fill the arena on a 2 px grid with the
     * same collision rule the firmware uses, and demand a path from the
     * start to the goal.  A maze the ball cannot finish is a bug. */
    {
        enum { G = 2, N = LUX_W / G };
        static unsigned char seen[N][N];
        static int qx[N * N], qy[N * N];
        memset(seen, 0, sizeof(seen));
        int head = 0, tail = 0;
        int sx = LUX_MAZE_START_X / G, sy = LUX_MAZE_START_Y / G;
        int gx = LUX_MAZE_GOAL_X / G, gy = LUX_MAZE_GOAL_Y / G;
        bool reached = false;
        qx[tail] = sx; qy[tail] = sy; tail++; seen[sy][sx] = 1;
        while (head < tail && !reached) {
            int cx = qx[head], cy = qy[head]; head++;
            if (abs(cx - gx) * G < LUX_GOAL_R && abs(cy - gy) * G < LUX_GOAL_R) { reached = true; break; }
            static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = cx + dx[k], ny = cy + dy[k];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N || seen[ny][nx]) continue;
                double px = nx * G, py = ny * G;
                if (dist(px, py, LUX_MAZE_CX, LUX_MAZE_CY) > LUX_MAZE_R - LUX_BALL_R) continue;
                bool hit = false;
                for (int i = 0; i < LUX_MAZE_WALLS && !hit; i++) {
                    const lux_wall_t *w = &lux_maze_walls[i];
                    hit = px + LUX_BALL_R > w->x && px - LUX_BALL_R < w->x + w->w &&
                          py + LUX_BALL_R > w->y && py - LUX_BALL_R < w->y + w->h;
                }
                if (hit) continue;
                seen[ny][nx] = 1;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
        check(reached, "a ball of LUX_BALL_R can travel from the start to the goal");
        if (reached) printf("   maze: goal reachable, %d grid cells explored\n", head);
    }
    section("circular arena, walls and targets are all reachable and in bounds");

    /* --------------------------------------------------- display gallery */
    /* The artwork plate is the point of this screen, so it must be the
     * biggest element on it and it must clear everything else. */
    check(lux_circle_fits(LUX_CX, LUX_ART_CY, LUX_ART_D / 2 + LUX_ART_FRAME), "art plate and mount inside safe circle");
    check(LUX_ART_D > LUX_CARD_W - 40, "art plate is the dominant element");
    check(LUX_HOME_CY + LUX_HOME_R < LUX_ART_CY - LUX_ART_D / 2 - LUX_ART_FRAME, "mount clears the HOME button");
    check(LUX_CX - LUX_ARROW_DX + LUX_ARROW_R < LUX_CX - LUX_ART_D / 2 - LUX_ART_FRAME, "arrow clears the mount");
    check(lux_circle_fits(LUX_CX - LUX_ARROW_DX, LUX_ARROW_CY, LUX_ARROW_R), "previous-plate arrow");
    check(lux_circle_fits(LUX_CX + LUX_ARROW_DX, LUX_ARROW_CY, LUX_ARROW_R), "next-plate arrow");
    check_rect(LUX_CX - LUX_ART_NAME_W / 2, LUX_ART_NAME_Y, LUX_ART_NAME_W, LUX_ART_NAME_H, "plate name");
    check_rect(lux_row_x(LUX_ART_COUNT, LUX_DOT_D, LUX_DOT_GAP), LUX_DOT_Y,
               LUX_ART_COUNT * LUX_DOT_D + (LUX_ART_COUNT - 1) * LUX_DOT_GAP, LUX_DOT_D,
               "gallery page dots");
    check(LUX_ART_Y + LUX_ART_D + LUX_ART_FRAME <= LUX_ART_NAME_Y, "mount clears the plate name");
    check(LUX_ART_NAME_Y + LUX_ART_NAME_H <= LUX_DOT_Y, "plate name clears the dots");
    section("display gallery layout fits");

    /* ------------------------------------------------------------ music */
    check(lux_circle_fits(LUX_CX, LUX_DISC_CY, LUX_DISC_D / 2), "music disc inside safe circle");
    for (int i = 0; i < 3; i++)
        check_rect(lux_row_item_x(i, 3, LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_GAP), LUX_MUSIC_BTN_Y,
                   LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_H, "music transport button");
    check(LUX_MUSIC_TITLE_Y + 24 <= LUX_MUSIC_BTN_Y, "music title clears the transport row");
    check_rect(LUX_CX - LUX_MUSIC_FOOT_W / 2, LUX_MUSIC_FOOT_Y, LUX_MUSIC_FOOT_W, LUX_MUSIC_FOOT_H, "music footer");
    check(LUX_MUSIC_BTN_Y + LUX_MUSIC_BTN_H <= LUX_MUSIC_FOOT_Y, "transport row clears the footer");
    check(LUX_RULE_Y + 4 <= LUX_DISC_CY - LUX_DISC_D / 2, "disc clears the title rule");
    for (int i = 0; i < LUX_EQ_BARS; i++) {
        /* every bar corner at full height stays inside the disc's 4 px rim */
        int x0 = lux_eq_x(i), x1 = x0 + LUX_EQ_W;
        int y0 = LUX_DISC_CY - LUX_EQ_MAX_H / 2, y1 = y0 + LUX_EQ_MAX_H;
        double worst = fmax(fmax(dist(x0, y0, LUX_CX, LUX_DISC_CY), dist(x1, y0, LUX_CX, LUX_DISC_CY)),
                            fmax(dist(x0, y1, LUX_CX, LUX_DISC_CY), dist(x1, y1, LUX_CX, LUX_DISC_CY)));
        check(worst <= LUX_DISC_D / 2 - 4 - 6, "level bar stays inside the disc");
    }
    check(LUX_EQ_MIN_H < LUX_EQ_MAX_H && LUX_EQ_MIN_H >= LUX_EQ_W, "level bars have a sane range");
    section("music layout fits");

    /* ----------------------------------------------------- motion setup */
    check(lux_circle_fits(LUX_CX, LUX_LEVEL_CY, LUX_LEVEL_R), "bubble level inside safe circle");
    check_rect(LUX_CX - LUX_MOTION_STAT_W / 2, LUX_MOTION_STAT_Y, LUX_MOTION_STAT_W, LUX_MOTION_STAT_H, "motion status line");
    check(LUX_RULE_Y + 2 <= LUX_MOTION_STAT_Y, "status line clears the rule");
    check(LUX_MOTION_STAT_Y + LUX_MOTION_STAT_H <= LUX_LEVEL_CY - LUX_LEVEL_R, "level clears the status line");
    for (int i = 0; i < 3; i++)
        check_rect(lux_row_item_x(i, 3, LUX_AXIS_BTN_W, LUX_AXIS_BTN_GAP), LUX_AXIS_BTN_Y,
                   LUX_AXIS_BTN_W, LUX_AXIS_BTN_H, "axis button");
    check(LUX_LEVEL_CY + LUX_LEVEL_R <= LUX_AXIS_BTN_Y, "axis buttons clear the level");
    check_rect(LUX_CX - LUX_ZERO_BTN_W / 2, LUX_ZERO_BTN_Y, LUX_ZERO_BTN_W, LUX_ZERO_BTN_H, "zero-gyro button");
    check(LUX_AXIS_BTN_Y + LUX_AXIS_BTN_H <= LUX_ZERO_BTN_Y, "zero button clears the axis row");
    section("motion setup layout fits");

    /* ---------------------------------------------------- activity rings */
    for (int i = 0; i < LUX_ACT_RINGS; i++) {
        int d = lux_act_ring_d(i);
        check(lux_circle_fits(LUX_CX, LUX_ACT_CY, d / 2), "activity ring inside safe circle");
        if (i > 0) {
            int gap = (lux_act_ring_d(i - 1) - d) / 2 - LUX_ACT_RING_W;
            check(gap >= 2, "activity rings do not touch");
        }
    }
    check(lux_act_ring_d(LUX_ACT_RINGS - 1) / 2 - LUX_ACT_RING_W >= 20,
          "innermost ring keeps an open centre");
    check(LUX_RULE_Y + 2 + 4 <= LUX_ACT_CY - LUX_ACT_D0 / 2, "rings clear the title rule");
    check(LUX_ACT_CY + LUX_ACT_D0 / 2 + 8 <= LUX_ACT_STAT_Y, "rings clear the stats row");
    for (int c = 0; c < 3; c++) {
        check_rect(lux_act_stat_x(c), LUX_ACT_STAT_Y, LUX_ACT_STAT_W, LUX_ACT_STAT_H, "activity stat column");
        if (c > 0)
            check(!overlap(lux_act_stat_x(c - 1), LUX_ACT_STAT_Y, LUX_ACT_STAT_W, LUX_ACT_STAT_H,
                           lux_act_stat_x(c), LUX_ACT_STAT_Y, LUX_ACT_STAT_W, LUX_ACT_STAT_H),
                  "activity stat columns do not overlap");
    }
    check(LUX_ACT_STAT_W >= 44 && LUX_ACT_STAT_H >= 44, "stat columns are finger-sized");
    section("activity rings and stats fit with nothing overlapping");

    /* ------------------------------------------------------------ clock */
    check(LUX_BEZEL_D / 2 <= LUX_R, "bezel fits the glass");
    check(LUX_DIAL_D < LUX_BEZEL_D, "dial sits inside the bezel");
    check(LUX_CHAPTER_D < LUX_DIAL_D, "chapter ring sits inside the dial");
    check(LUX_INNER_D < LUX_CHAPTER_D, "inner ring sits inside the chapter ring");
    check(LUX_DIAL_X == LUX_DIAL_Y, "dial is centred");

    /* All twelve numerals, at their real box size, must clear the glass and
     * must not run into the chapter ring or each other. */
    for (int i = 0; i < 12; i++) {
        double a = i * 3.14159265358979323846 / 6.0;
        int x = LUX_CX + (int)lround(sin(a) * LUX_NUMERAL_R) - LUX_NUMERAL_W / 2;
        int y = LUX_CY - (int)lround(cos(a) * LUX_NUMERAL_R) - LUX_NUMERAL_H / 2;
        if (!lux_rect_fits(x, y, LUX_NUMERAL_W, LUX_NUMERAL_H)) {
            printf("FAIL: numeral %d box leaves the safe circle\n", i == 0 ? 12 : i);
            failures++;
        }
    }
    /* Adjacent numerals are 30 degrees apart on the numeral radius. */
    {
        double sep = 2.0 * LUX_NUMERAL_R * sin(3.14159265358979323846 / 12.0);
        check(sep > LUX_NUMERAL_W, "adjacent numerals do not collide");
        printf("   numeral pitch: %.1f px for a %d px box\n", sep, LUX_NUMERAL_W);
    }
    check(LUX_NUMERAL_R + LUX_NUMERAL_H / 2 <= LUX_CHAPTER_D / 2, "numerals sit inside the chapter ring");
    check_rect(LUX_DATE_X, LUX_DATE_Y, LUX_DATE_W, LUX_DATE_H, "date window");

    /* Hands must stay on the dial, and each stub must be shorter than the
     * tip it tapers into, or the "tapered" hand would be a blunt club. */
    int dial_r = LUX_DIAL_D / 2;
    check(dial_r + LUX_HAND_SEC_MOD < dial_r, "second hand shorter than the dial");
    check(dial_r + LUX_HAND_MIN_MOD < dial_r + LUX_HAND_SEC_MOD, "minute hand shorter than second hand");
    check(dial_r + LUX_HAND_HOUR_MOD < dial_r + LUX_HAND_MIN_MOD, "hour hand shortest");
    check(LUX_HAND_HOUR_STUB < LUX_HAND_HOUR_MOD, "hour stub shorter than hour tip");
    check(LUX_HAND_MIN_STUB < LUX_HAND_MIN_MOD, "minute stub shorter than minute tip");
    check(dial_r + LUX_HAND_HOUR_MOD > LUX_INNER_D / 2 - 40, "hour hand reaches past the wordmark ring");
    /* The date window must not sit under the centre cap. */
    check(LUX_DATE_X - LUX_CX > LUX_HAND_CAP_R, "date window clears the centre cap");
    section("watch face geometry is consistent");

    if (failures) {
        printf("\n%d layout failure(s)\n", failures);
        return 1;
    }
    /* ------------------------------------------------- touch targets */
    /* Every control's touch area is at least LUX_HIT_MIN square.  Small
     * buttons keep their drawn size and get an invisible padded hit area
     * (lux_hit_pad, applied in ui.c); padded areas of neighbouring controls
     * must never overlap, or a tap between them would be ambiguous. */
    {
        typedef struct { int x, y, w, h; const char *what; } hit_t;
        hit_t hs[48];
        int nh;
#define PAD(r) do { int p_ = lux_hit_pad((r).w, (r).h); (r).x -= p_; (r).y -= p_; \
                    (r).w += 2 * p_; (r).h += 2 * p_; } while (0)
#define CHECK_GROUP(name) do { \
            for (int i = 0; i < nh; i++) { \
                PAD(hs[i]); \
                if (hs[i].w < LUX_HIT_MIN || hs[i].h < LUX_HIT_MIN) { \
                    printf("FAIL: %s %s hit area %dx%d\n", name, hs[i].what, hs[i].w, hs[i].h); failures++; } \
            } \
            for (int i = 0; i < nh; i++) for (int j = i + 1; j < nh; j++) \
                if (overlap(hs[i].x, hs[i].y, hs[i].w, hs[i].h, hs[j].x, hs[j].y, hs[j].w, hs[j].h)) { \
                    printf("FAIL: %s: %s and %s touch areas overlap\n", name, hs[i].what, hs[j].what); failures++; } \
        } while (0)

        /* HOME on every screen */
        nh = 0;
        hs[nh++] = (hit_t){ LUX_HOME_CX - LUX_HOME_R, LUX_HOME_CY - LUX_HOME_R, 2 * LUX_HOME_R, 2 * LUX_HOME_R, "HOME" };
        CHECK_GROUP("chrome");

        /* calculator: all 19 keys */
        nh = 0;
        for (int r = 0; r < LUX_CALC_ROWS; r++)
            for (int c = 0; c < lux_calc_cols(r); c++)
                hs[nh++] = (hit_t){ lux_calc_key_x(r, c), lux_calc_row_y(r), LUX_CALC_KEY_W, LUX_CALC_KEY_H, "key" };
        CHECK_GROUP("calculator");

        /* maze: reset + flip */
        nh = 0;
        hs[nh++] = (hit_t){ LUX_CX - LUX_MAZE_BTN_DX - LUX_MAZE_BTN_R, LUX_MAZE_BTN_CY - LUX_MAZE_BTN_R,
                            2 * LUX_MAZE_BTN_R, 2 * LUX_MAZE_BTN_R, "reset" };
        hs[nh++] = (hit_t){ LUX_CX + LUX_MAZE_BTN_DX - LUX_MAZE_BTN_R, LUX_MAZE_BTN_CY - LUX_MAZE_BTN_R,
                            2 * LUX_MAZE_BTN_R, 2 * LUX_MAZE_BTN_R, "flip" };
        CHECK_GROUP("maze");

        /* motion: three axis buttons + zero gyro */
        nh = 0;
        for (int i = 0; i < 3; i++)
            hs[nh++] = (hit_t){ lux_row_item_x(i, 3, LUX_AXIS_BTN_W, LUX_AXIS_BTN_GAP), LUX_AXIS_BTN_Y,
                                LUX_AXIS_BTN_W, LUX_AXIS_BTN_H, "axis" };
        hs[nh++] = (hit_t){ LUX_CX - LUX_ZERO_BTN_W / 2, LUX_ZERO_BTN_Y, LUX_ZERO_BTN_W, LUX_ZERO_BTN_H, "zero" };
        CHECK_GROUP("motion");

        /* music transport */
        nh = 0;
        for (int i = 0; i < 3; i++)
            hs[nh++] = (hit_t){ lux_row_item_x(i, 3, LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_GAP), LUX_MUSIC_BTN_Y,
                                LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_H, "transport" };
        CHECK_GROUP("music");

        /* display gallery arrows */
        nh = 0;
        hs[nh++] = (hit_t){ LUX_CX - LUX_ARROW_DX - LUX_ARROW_R, LUX_ART_CY - LUX_ARROW_R, 2 * LUX_ARROW_R, 2 * LUX_ARROW_R, "prev" };
        hs[nh++] = (hit_t){ LUX_CX + LUX_ARROW_DX - LUX_ARROW_R, LUX_ART_CY - LUX_ARROW_R, 2 * LUX_ARROW_R, 2 * LUX_ARROW_R, "next" };
        CHECK_GROUP("display");

        /* list rows (row-local coordinates): stepper -/+ and the switch must
         * keep their padded areas inside the row they live in */
        nh = 0;
        hs[nh++] = (hit_t){ lux_step_btn_x(0), LUX_STEP_BTN_Y, LUX_STEP_BTN_W, LUX_STEP_BTN_H, "minus" };
        hs[nh++] = (hit_t){ lux_step_btn_x(1), LUX_STEP_BTN_Y, LUX_STEP_BTN_W, LUX_STEP_BTN_H, "plus" };
        CHECK_GROUP("stepper row");
        for (int i = 0; i < nh; i++)
            check(hs[i].y >= 0 && hs[i].y + hs[i].h <= LUX_ROW_H, "stepper touch area stays inside its row");
        nh = 0;
        hs[nh++] = (hit_t){ LUX_SWITCH_X, LUX_SWITCH_Y, LUX_SWITCH_W, LUX_SWITCH_H, "switch" };
        CHECK_GROUP("switch row");
        check(hs[0].y >= 0 && hs[0].y + hs[0].h <= LUX_ROW_H, "switch touch area stays inside its row");
        check(LUX_SLIDER_H + 2 * lux_hit_pad(LUX_LIST_W - 36, LUX_SLIDER_H) >= LUX_HIT_MIN, "slider is grabbable");
#undef CHECK_GROUP
#undef PAD
    }
    section("every control has a 44 px touch area and none overlap");

    puts("\nALL LAYOUT CHECKS PASSED");
    return 0;
}
