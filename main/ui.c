/*
 * Luxor user interface.
 *
 * Design rules this file follows without exception:
 *   1. Every coordinate comes from layout.h, which is validated on the host by
 *      tests/test_layout.c.  No number is typed in twice and no rectangle is
 *      placed by eye.
 *   2. Labels are only rewritten when their text actually changes, so a 20 Hz
 *      render loop does not invalidate the whole screen 20 times a second.
 *   3. Tilt arrives already converted to screen directions (see motion.h), so
 *      nothing in here has to reason about sensor axes.
 */
#include "ui.h"
#include "layout.h"
#include "battery.h"
#include "version.h"
#include "luxconf.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "lvgl.h"

#define THEME_COUNT  6
#define MUSIC_TRACKS 4
#define PI_F         3.14159265358979323846f

/* ------------------------------------------------------------- palette
 *
 * Luxor is white, black and gold.  White is the field, black is the type,
 * gold is the only colour -- used for rules, rims, hands and the one thing
 * on a screen that matters.  Nothing else gets a colour, which is what makes
 * the gold read as gold rather than as decoration.
 *
 * Two golds, because one cannot do both jobs on a white field:
 *   LUX_GOLD_DEEP  passes as text and hairlines on white
 *   LUX_GOLD       is the leaf tone, for fills and rims
 */

#define LUX_PAPER      0xFFFFFFu   /* the field                            */
#define LUX_PAPER_2    0xF1EDE3u   /* barely-there warm tint, for panels   */
#define LUX_PAPER_3    0xE4DECEu   /* hairline / disabled                  */
#define LUX_INK        0x0A0A0Au   /* type                                 */
#define LUX_INK_SOFT   0x5C5648u   /* secondary type                       */
#define LUX_GOLD       0xD4AF37u   /* leaf: fills, rims, hands             */
#define LUX_GOLD_DEEP  0xA07C14u   /* readable gold on white               */
#define LUX_GOLD_LIGHT 0xF2D675u   /* highlight on dark only               */
#define LUX_BLACK      0x050505u
#define LUX_WHITE      0xF7F4ECu
#define LUX_MUTED      0xB7AE96u
#define LUX_DIM        0x6E6A5Fu
#define LUX_LOW        0xC0392Bu   /* low-battery red, the only red chrome */

typedef struct {
    const char *name;
    const char *caption;
    uint32_t bg0, bg1, panel, panel2, text, muted, accent, accent2;
    bool light;                 /* true when the field is paper, not ink */
} luxor_theme_t;

static const luxor_theme_t themes[THEME_COUNT] = {
    /* Default.  White field, black type, gold accents. */
    { "Ivory Gold",     "white, black and gold",
      LUX_PAPER, 0xF6F2E9, LUX_PAPER,  LUX_PAPER_2, LUX_INK,   LUX_INK_SOFT,
      LUX_GOLD_DEEP, LUX_GOLD,        true  },
    { "Porcelain",      "white and graphite",
      0xFFFFFF,  0xF2F2F2, 0xFFFFFF,   0xEDEDED,    0x101010,  0x606060,
      0x2E2E2E,      0x8A8A8A,        true  },
    { "Noir Gold",      "the dark finish",
      0x020202,  0x12100B, 0x111111,   0x1E1E1E,    LUX_WHITE, LUX_MUTED,
      LUX_GOLD,      LUX_GOLD_LIGHT,  false },
    { "Midnight Gold",  "deep blue at night",
      0x01040A,  0x0C1626, 0x0E1524,   0x16213A,    0xEAF0FF,  0x8FA2C4,
      LUX_GOLD,      LUX_GOLD_LIGHT,  false },
    { "Champagne Noir", "warm, low contrast",
      0x0A0804,  0x241A09, 0x181209,   0x261D0E,    0xFBF3E2,  0xB9A98A,
      LUX_GOLD,      LUX_GOLD_LIGHT,  false },
    { "Monochrome",     "no colour at all",
      0x030303,  0x161616, 0x121212,   0x1F1F1F,    0xFFFFFF,  0x9A9A9A,
      0xE6E6E6,      0xFFFFFF,        false },
};

/* Gallery plates, drawn procedurally -- see draw_art_plate(). */
static const char *art_names[LUX_ART_COUNT] = {
    "Sunburst", "Orbit", "Guilloche", "Dune", "Atrium", "Meridian",
};

static const char *music_names[MUSIC_TRACKS] = { "Pulse Grid", "Neon Run", "Slow Orbit", "Arcade Spark" };

/* ----------------------------------------------------------- ui state */

static luxor_app_t current_app = LUXOR_APP_HOME;
static int requested_app = -1;
static bool rebuild_requested;
static uint8_t theme_index;
static luxor_ui_snapshot_t snap;      /* most recent frame, used when building */

#define CMD_Q 12
static luxor_ui_event_t cmd_q[CMD_Q];
static unsigned cmd_head, cmd_tail;

/* Widgets of the current screen.  Cleared wholesale on every rebuild. */
static struct {
    lv_obj_t *l1, *l2, *l3, *l4, *l5, *l6;
    lv_obj_t *arc, *dial, *ball, *goal, *dot, *calc_disp, *toast, *art;
    lv_obj_t *hand_h, *hand_hs, *hand_m, *hand_ms, *hand_s, *cap;
    lv_obj_t *batt_txt, *batt_body, *batt_nub, *batt_fill;
    int batt_shown;          /* last drawn percent, INT32_MIN = never */
    lv_point_t hand_pts[5][2];
    int32_t shown_h, shown_m, shown_s;
} w;

static char toast_text[64];
static uint32_t toast_until;

/* ------------------------------------------------------------ helpers */

static lv_color_t col(uint32_t c) { return lv_color_hex(c); }
static const luxor_theme_t *th(void) { return &themes[theme_index % THEME_COUNT]; }

/* Typography: Inter (SIL Open Font License), generated for LVGL by
 * lv_font_conv into main/fonts/ with the LVGL symbol glyphs merged in, so
 * LV_SYMBOL_* still render.  Text sizes use Inter Medium, the big sizes use
 * Inter Display, which is drawn for large settings.  See fonts/README.md. */
LV_FONT_DECLARE(lux_inter_14);
LV_FONT_DECLARE(lux_inter_16);
LV_FONT_DECLARE(lux_inter_20);
LV_FONT_DECLARE(lux_inter_28);
LV_FONT_DECLARE(lux_inter_48);

static const lv_font_t *f14(void) { return &lux_inter_14; }
static const lv_font_t *f16(void) { return &lux_inter_16; }
static const lv_font_t *f20(void) { return &lux_inter_20; }
static const lv_font_t *f28(void) { return &lux_inter_28; }
static const lv_font_t *f48(void) { return &lux_inter_48; }

/* Only touch the label when the text really changed: lv_label_set_text always
 * invalidates, and the render loop runs at up to 20 Hz. */
static void set_text(lv_obj_t *l, const char *s)
{
    if (!l || !s) return;
    const char *cur = lv_label_get_text(l);
    if (cur && strcmp(cur, s) == 0) return;
    lv_label_set_text(l, s);
}

static void emit(luxor_ui_command_t cmd, int32_t value)
{
    /* Coalesce: a slider drag should not fill the queue with stale values. */
    for (unsigned i = cmd_tail; i != cmd_head; i = (i + 1) % CMD_Q) {
        if (cmd_q[i].cmd == cmd) { cmd_q[i].value = value; return; }
    }
    unsigned next = (cmd_head + 1) % CMD_Q;
    if (next == cmd_tail) return;                 /* full: drop, never block */
    cmd_q[cmd_head].cmd = cmd;
    cmd_q[cmd_head].value = value;
    cmd_head = next;
}

bool luxor_ui_take_command(luxor_ui_event_t *out)
{
    if (cmd_tail == cmd_head) return false;
    if (out) *out = cmd_q[cmd_tail];
    cmd_tail = (cmd_tail + 1) % CMD_Q;
    return true;
}

void luxor_ui_toast(const char *text)
{
    if (!text) return;
    snprintf(toast_text, sizeof(toast_text), "%s", text);
    toast_until = lv_tick_get() + 2600;
}

/* ---------------------------------------------------- generic widgets */

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int wdt, int hgt,
                          const char *txt, const lv_font_t *font,
                          lv_text_align_t align, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent ? parent : lv_scr_act());
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, wdt, hgt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

/* Centre a label of the given size on a point. */
static lv_obj_t *mk_label_c(lv_obj_t *parent, int cx, int cy, int wdt, int hgt,
                            const char *txt, const lv_font_t *font, uint32_t color)
{
    return mk_label(parent, cx - wdt / 2, cy - hgt / 2, wdt, hgt, txt, font,
                    LV_TEXT_ALIGN_CENTER, color);
}

/* Whichever of ink/paper still reads against a filled accent button. */
static uint32_t on_accent(void)
{
    return th()->light ? LUX_PAPER : LUX_BLACK;
}

/* Press feedback shared by every button: the control settles inwards by a
 * pixel on each side and eases back, the way a physical key does.  It uses
 * transform_width/height rather than a zoom so no layer buffer is needed. */
static lv_style_transition_dsc_t press_tr;
static lv_style_t press_style;
static bool press_style_ready;

static void press_style_init(void)
{
    if (press_style_ready) return;
    static const lv_style_prop_t props[] = {
        LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_TRANSFORM_ZOOM,
        LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, 0 };
    lv_style_transition_dsc_init(&press_tr, props, lv_anim_path_ease_out, 90, 0, NULL);
    lv_style_init(&press_style);
    lv_style_set_transition(&press_style, &press_tr);
    press_style_ready = true;
}

static void add_press_feedback(lv_obj_t *o, bool shrink_box)
{
    press_style_init();
    lv_obj_add_style(o, &press_style, 0);
    lv_obj_add_style(o, &press_style, LV_STATE_PRESSED);
    if (shrink_box) {
        lv_obj_set_style_transform_width(o, -2, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(o, -2, LV_STATE_PRESSED);
    }
}

static void style_button(lv_obj_t *b, bool accent)
{
    const luxor_theme_t *t = th();
    /* Pill-shaped: fully rounded ends read as one calm family of controls. */
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    add_press_feedback(b, true);
    lv_obj_set_style_bg_color(b, col(accent ? t->accent : t->panel2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* An outlined button needs a visible edge on a white field, where a
     * 40 per-cent gold hairline simply disappears. */
    lv_obj_set_style_border_width(b, accent ? 0 : (t->light ? 2 : 1), 0);
    lv_obj_set_style_border_color(b, col(t->accent), 0);
    lv_obj_set_style_border_opa(b, t->light ? LV_OPA_80 : LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, col(accent ? t->accent2 : t->accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, accent ? LV_OPA_COVER : LV_OPA_30, LV_STATE_PRESSED);
}

static void cmd_cb(lv_event_t *e)
{
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    emit((luxor_ui_command_t)(packed & 0xFFFFu), (int32_t)(packed >> 16));
}

/* Size a control and give it at least a LUX_HIT_MIN square of touch area.
 * The padding is invisible (LVGL's extended click area); test_layout.c
 * proves the padded areas of neighbours never overlap.  Because
 * luxor_ui_tap_is_background() uses the same hit test, a tap just beside a
 * small button counts as that button, not as a background gesture. */
static void size_control(lv_obj_t *o, int wdt, int hgt)
{
    lv_obj_set_size(o, wdt, hgt);
    int pad = lux_hit_pad(wdt, hgt);
    if (pad) lv_obj_set_ext_click_area(o, pad);
}

static lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int wdt, int hgt,
                           const char *txt, const lv_font_t *font, bool accent,
                           luxor_ui_command_t cmd, int32_t value)
{
    lv_obj_t *b = lv_btn_create(parent ? parent : lv_scr_act());
    lv_obj_set_pos(b, x, y);
    size_control(b, wdt, hgt);
    style_button(b, accent);
    if (cmd != LUXOR_UI_NONE) {
        uintptr_t packed = (uintptr_t)cmd | ((uintptr_t)(uint16_t)value << 16);
        lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, (void *)packed);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(accent ? on_accent() : th()->text), 0);
    lv_obj_center(l);
    return b;
}

/* The gesture rule, in one place:
 *
 *   1 tap   on a dark screen    wakes it
 *   2 taps  on empty background  go home
 *   3 taps  on empty background  turn the watch off / on
 *
 * "Empty background" means the finger did not land on anything you can
 * press.  Walk up from the deepest object under the finger: if any of it,
 * short of the screen itself, is clickable, the touch belongs to a control. */
bool luxor_ui_tap_is_background(int16_t x, int16_t y)
{
    lv_obj_t *scr = lv_scr_act();
    if (!scr) return true;
    lv_point_t p = { x, y };
    lv_obj_t *o = lv_indev_search_obj(scr, &p);
    for (; o && o != scr; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) return false;
    return true;
}

/* Point-in-flat-top-hexagon, in the tile's own coordinates.  A flat-top
 * hexagon of circumradius R is |y| <= sqrt(3)/2 R and |x| + |y|/sqrt(3) <= R. */
static void hex_hit_test_cb(lv_event_t *e)
{
    lv_hit_test_info_t *info = lv_event_get_param(e);
    lv_obj_t *tile = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(tile, &a);
    float cx = (a.x1 + a.x2) * 0.5f, cy = (a.y1 + a.y2) * 0.5f;
    float x = fabsf(info->point->x - cx), y = fabsf(info->point->y - cy);
    const float R = LUX_HEX_DRAW_R + 2.0f;      /* a touch of slack */
    info->res = (y <= 0.8660254f * R) && (x + y * 0.5773503f <= R);
}

static void open_cb(lv_event_t *e) { requested_app = (int)(intptr_t)lv_event_get_user_data(e); }

static lv_obj_t *mk_round_button(lv_obj_t *parent, int cx, int cy, int r,
                                 const char *glyph, bool accent)
{
    lv_obj_t *b = lv_btn_create(parent ? parent : lv_scr_act());
    lv_obj_set_pos(b, cx - r, cy - r);
    size_control(b, 2 * r, 2 * r);
    style_button(b, accent);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, glyph);
    lv_obj_set_style_text_color(l, col(accent ? on_accent() : th()->accent), 0);
    lv_obj_center(l);
    return b;
}

/* The one and only HOME control, in the same place on every screen. */
static void add_home_chip(void)
{
    lv_obj_t *b = mk_round_button(NULL, LUX_HOME_CX, LUX_HOME_CY, LUX_HOME_R, LV_SYMBOL_HOME, false);
    lv_obj_add_event_cb(b, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LUXOR_APP_HOME);
}

/* A short gold rule, centred.  It separates the title from the content the
 * way a caption used to, without putting a second word on the screen. */
static void add_rule(int y, int wdt)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(r, LUX_CX - wdt / 2, y);
    lv_obj_set_size(r, wdt, 2);
    lv_obj_set_style_radius(r, 1, 0);
    lv_obj_set_style_bg_color(r, col(t->accent), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/* Title only.  Every screen says what it is exactly once. */
static void app_header(const char *title)
{
    const luxor_theme_t *t = th();
    add_home_chip();
    lv_obj_t *l = mk_label(NULL, LUX_TITLE_X, LUX_TITLE_Y, LUX_TITLE_W, LUX_TITLE_H,
                           title, f20(), LV_TEXT_ALIGN_CENTER, t->text);
    /* Letter-spaced small caps read as a masthead rather than a form label. */
    lv_obj_set_style_text_letter_space(l, 3, 0);
    add_rule(LUX_RULE_Y, LUX_RULE_W);
}

static lv_obj_t *mk_card(int x, int y, int wdt, int hgt)
{
    const luxor_theme_t *t = th();
    lv_obj_t *c = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, wdt, hgt);
    lv_obj_set_style_radius(c, 26, 0);
    lv_obj_set_style_bg_color(c, col(t->panel2), 0);
    lv_obj_set_style_bg_opa(c, t->light ? LV_OPA_COVER : LV_OPA_80, 0);
    lv_obj_set_style_border_width(c, t->light ? 2 : 1, 0);
    lv_obj_set_style_border_color(c, col(t->accent), 0);
    lv_obj_set_style_border_opa(c, t->light ? LV_OPA_60 : LV_OPA_30, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* ------------------------------------------------------- time display
 *
 * A centred time label wanders: "1:05" and "12:48" are different widths, so
 * the colon slides left and right as the hour changes and the whole face
 * looks loose.  Splitting it at the colon and anchoring the two halves to a
 * fixed axis keeps the colon nailed to one column all day, which is what
 * makes a printed timetable or a real watch dial look square.
 *
 *      hours + colon   right-aligned, ending at the axis
 *      minutes         left-aligned,  starting at the axis
 *      meridiem        a small subordinate mark, fixed offset
 */
typedef struct {
    lv_obj_t *hh, *mm, *ap;
} lux_timebox_t;

static lux_timebox_t mk_timebox(int axis_x, int y, int hgt, int reach,
                                const lv_font_t *font, uint32_t colour,
                                const lv_font_t *small, uint32_t small_colour)
{
    lux_timebox_t tb;
    tb.hh = mk_label(NULL, axis_x - reach, y, reach, hgt,
                     "--:", font, LV_TEXT_ALIGN_RIGHT, colour);
    tb.mm = mk_label(NULL, axis_x, y, reach, hgt,
                     "--", font, LV_TEXT_ALIGN_LEFT, colour);
    tb.ap = small ? mk_label(NULL, axis_x + 46, y + 4, 34, 18,
                             "", small, LV_TEXT_ALIGN_LEFT, small_colour)
                  : NULL;
    return tb;
}

/* Fill a time box from a struct tm.  Returns nothing; all three labels are
 * change-guarded by set_text so a static minute costs no redraw. */
static void timebox_set(const lux_timebox_t *tb, const struct tm *tv,
                        bool h24, bool valid)
{
    char a[8], b[8];
    if (!valid) {
        set_text(tb->hh, "--:");
        set_text(tb->mm, "--");
        if (tb->ap) set_text(tb->ap, "");
        return;
    }
    if (h24) {
        snprintf(a, sizeof(a), "%02d:", tv->tm_hour);
        snprintf(b, sizeof(b), "%02d", tv->tm_min);
        if (tb->ap) set_text(tb->ap, "");
    } else {
        int h12 = tv->tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(a, sizeof(a), "%d:", h12);
        snprintf(b, sizeof(b), "%02d", tv->tm_min);
        if (tb->ap) set_text(tb->ap, tv->tm_hour < 12 ? "AM" : "PM");
    }
    set_text(tb->hh, a);
    set_text(tb->mm, b);
}

/* ------------------------------------------------------ hexagon sprite */

#define HEXW (LUX_HEX_W + 2)      /* 98 */
#define HEXH (LUX_HEX_H + 3)      /* 86 */

static uint8_t *hex_buf[2];
static lv_img_dsc_t hex_dsc[2];
static bool hex_ok;
static int hex_theme = -1;

static void hex_points(lv_point_t *p, float cx, float cy, float r)
{
    /* Flat-top hexagon: vertices at 0, 60, ... 300 degrees. */
    for (int i = 0; i < 6; i++) {
        float a = (float)i * PI_F / 3.0f;
        p[i].x = (lv_coord_t)lroundf(cx + r * cosf(a));
        p[i].y = (lv_coord_t)lroundf(cy + r * sinf(a));
    }
}

static void build_hex_sprites(void)
{
    if (hex_ok && hex_theme == (int)theme_index) return;

    size_t sz = (size_t)LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(HEXW, HEXH);
    for (int k = 0; k < 2; k++) {
        if (hex_buf[k]) continue;
        hex_buf[k] = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!hex_buf[k]) hex_buf[k] = heap_caps_malloc(sz, MALLOC_CAP_DEFAULT);
        if (!hex_buf[k]) { hex_ok = false; return; }      /* fall back to plain tiles */
    }

    const luxor_theme_t *t = th();
    lv_obj_t *cv = lv_canvas_create(lv_scr_act());
    lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);

    for (int k = 0; k < 2; k++) {
        lv_canvas_set_buffer(cv, hex_buf[k], HEXW, HEXH, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);

        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_opa = LV_OPA_COVER;
        lv_point_t pts[6];

        /* Three passes: a gold rim, then the face inset 2 px, which reads as
         * an engraved edge without a second sprite or any run-time scaling.
         * k == 1 is the centre tile, which is filled gold instead of outlined
         * so the launcher has exactly one focal point.
         *
         * On a white field the face is paper and the rim carries the gold; on
         * a dark field that inverts, or the tiles would vanish. */
        bool light = t->light;
        uint32_t rim  = k ? t->accent : t->accent;
        uint32_t face = k ? t->accent2
                          : (light ? t->panel : t->panel);

        d.bg_color = col(rim);
        hex_points(pts, HEXW / 2.0f, HEXH / 2.0f, (float)LUX_HEX_DRAW_R - 0.5f);
        lv_canvas_draw_polygon(cv, pts, 6, &d);

        d.bg_color = col(face);
        hex_points(pts, HEXW / 2.0f, HEXH / 2.0f, (float)LUX_HEX_DRAW_R - 2.0f);
        lv_canvas_draw_polygon(cv, pts, 6, &d);

        /* A one-pixel highlight along the top edge lifts the tile off a white
         * background, which otherwise swallows a flat shape completely. */
        if (light && !k) {
            d.bg_color = col(LUX_PAPER);
            hex_points(pts, HEXW / 2.0f, HEXH / 2.0f - 1.0f, (float)LUX_HEX_DRAW_R - 4.0f);
            lv_canvas_draw_polygon(cv, pts, 6, &d);
        }

        hex_dsc[k].header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        hex_dsc[k].header.always_zero = 0;
        hex_dsc[k].header.reserved = 0;
        hex_dsc[k].header.w = HEXW;
        hex_dsc[k].header.h = HEXH;
        hex_dsc[k].data_size = sz;
        hex_dsc[k].data = hex_buf[k];
    }
    lv_obj_del(cv);
    lv_img_cache_invalidate_src(NULL);   /* the buffers changed under the cache */
    hex_ok = true;
    hex_theme = (int)theme_index;
}

/* ------------------------------------------------------- screen setup */

/* Every gradient is dithered.  In 16-bit colour a subtle gradient such as
 * Noir Gold's 0x020202 -> 0x12100B has only one or two RGB565 steps, so
 * undithered it draws as a hard line straight across the middle of the
 * screen.  Needs CONFIG_LV_DITHER_GRADIENT=y (sdkconfig.defaults). */
static void apply_background(void)
{
    const luxor_theme_t *t = th();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, col(t->bg0), 0);
    lv_obj_set_style_bg_grad_color(scr, col(t->bg1), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(scr, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, col(t->text), 0);
    lv_obj_set_style_text_font(scr, f14(), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

static void screen_base(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    memset(&w, 0, sizeof(w));
    w.shown_h = w.shown_m = w.shown_s = INT32_MIN;
    w.batt_shown = INT32_MIN;
    apply_background();
}

/* A hairline ring just inside the glass ties every screen together and makes
 * a rotated panel immediately obvious during bring-up. */
static void add_edge_ring(uint8_t opa)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = lv_obj_create(lv_scr_act());
    int d = 2 * LUX_DECOR_R;
    lv_obj_set_size(r, d, d);
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, col(t->accent), 0);
    lv_obj_set_style_border_opa(r, t->light ? (lv_opa_t)(opa + 40) : opa, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/* ------------------------------------------------- scrollable settings */

static lv_obj_t *mk_list(void)
{
    const luxor_theme_t *t = th();
    lv_obj_t *l = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(l, LUX_LIST_X, LUX_LIST_Y);
    lv_obj_set_size(l, LUX_LIST_W, LUX_LIST_H);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_row(l, 6, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(l, col(t->accent), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(l, LV_OPA_40, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(l, 3, LV_PART_SCROLLBAR);
    return l;
}

static lv_obj_t *mk_row(lv_obj_t *list, int hgt)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_set_size(r, LUX_LIST_W - 8, hgt);
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_set_style_bg_color(r, col(t->panel), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

/* --- steppers ---------------------------------------------------------- */

typedef struct {
    luxor_ui_command_t cmd;
    int32_t value, step, min, max;
    const char *fmt;
    lv_obj_t *lbl;
    bool clamp_date;
} stepper_t;

#define MAX_STEPPERS 8
static stepper_t steppers[MAX_STEPPERS];
static int stepper_count;

/* Time editor fields, shared with the date clamp below. */
static int edit_year = 2026, edit_mon = 1, edit_day = 1, edit_hour = 0, edit_min = 0;

static int days_in_month(int y, int m)
{
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    if (m < 1 || m > 12) return 31;
    return len[m - 1];
}

static void stepper_refresh(int i)
{
    if (i < 0 || i >= stepper_count || !steppers[i].lbl) return;
    char b[24];
    snprintf(b, sizeof(b), steppers[i].fmt, (int)steppers[i].value);
    set_text(steppers[i].lbl, b);
}

static void stepper_cb(lv_event_t *e)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int i = (int)(packed >> 1);
    int dir = (packed & 1) ? 1 : -1;
    if (i < 0 || i >= stepper_count) return;

    stepper_t *s = &steppers[i];
    s->value += dir * s->step;
    if (s->value < s->min) s->value = s->min;
    if (s->value > s->max) s->value = s->max;

    if (s->clamp_date) {
        /* Keep the editor honest: 31 February is not a date. */
        edit_year = steppers[0].value;
        edit_mon  = steppers[1].value;
        int limit = days_in_month(edit_year, edit_mon);
        if (steppers[2].value > limit) { steppers[2].value = limit; stepper_refresh(2); }
        steppers[2].max = limit;
        edit_day  = steppers[2].value;
        edit_hour = steppers[3].value;
        edit_min  = steppers[4].value;
    }
    stepper_refresh(i);
    if (s->cmd != LUXOR_UI_NONE) emit(s->cmd, s->value);
}

static void row_stepper(lv_obj_t *list, const char *title, luxor_ui_command_t cmd,
                        int32_t value, int32_t step, int32_t min, int32_t max,
                        const char *fmt, bool clamp_date)
{
    if (stepper_count >= MAX_STEPPERS) return;
    const luxor_theme_t *t = th();
    int idx = stepper_count++;
    stepper_t *s = &steppers[idx];
    s->cmd = cmd; s->value = value; s->step = step; s->min = min; s->max = max;
    s->fmt = fmt; s->clamp_date = clamp_date;

    lv_obj_t *r = mk_row(list, 54);
    mk_label(r, 12, 8, 150, 18, title, f14(), LV_TEXT_ALIGN_LEFT, t->muted);
    char b[24];
    snprintf(b, sizeof(b), fmt, (int)value);
    s->lbl = mk_label(r, 12, 27, 150, 22, b, f20(), LV_TEXT_ALIGN_LEFT, t->text);

    lv_obj_t *minus = lv_btn_create(r);
    lv_obj_set_pos(minus, lux_step_btn_x(0), LUX_STEP_BTN_Y);
    size_control(minus, LUX_STEP_BTN_W, LUX_STEP_BTN_H);
    style_button(minus, false);
    lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(minus, stepper_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(idx * 2));
    lv_obj_t *ml = lv_label_create(minus); lv_label_set_text(ml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(ml, col(t->accent), 0); lv_obj_center(ml);

    lv_obj_t *plus = lv_btn_create(r);
    lv_obj_set_pos(plus, lux_step_btn_x(1), LUX_STEP_BTN_Y);
    size_control(plus, LUX_STEP_BTN_W, LUX_STEP_BTN_H);
    style_button(plus, false);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(plus, stepper_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(idx * 2 + 1));
    lv_obj_t *pl = lv_label_create(plus); lv_label_set_text(pl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(pl, col(t->accent), 0); lv_obj_center(pl);
}

/* --- sliders and switches ---------------------------------------------- */

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    luxor_ui_command_t cmd = (luxor_ui_command_t)(intptr_t)lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value(s);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(s);
    if (lbl) {
        char b[16];
        snprintf(b, sizeof(b), "%d%%", (int)v);
        set_text(lbl, b);
    }
    emit(cmd, v);
}

static void row_slider(lv_obj_t *list, const char *title, int min, int max,
                       int value, luxor_ui_command_t cmd)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = mk_row(list, 62);
    mk_label(r, 14, 8, 150, 18, title, f14(), LV_TEXT_ALIGN_LEFT, t->muted);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", value);
    lv_obj_t *val = mk_label(r, LUX_LIST_W - 80, 8, 60, 18, b, f14(), LV_TEXT_ALIGN_RIGHT, t->text);

    lv_obj_t *s = lv_slider_create(r);
    lv_obj_set_pos(s, 14, 34);
    size_control(s, LUX_LIST_W - 36, LUX_SLIDER_H);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_user_data(s, val);
    lv_obj_set_style_bg_color(s, col(t->panel2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, col(t->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, col(t->accent2), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)cmd);
}

static void switch_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    luxor_ui_command_t cmd = (luxor_ui_command_t)(intptr_t)lv_event_get_user_data(e);
    emit(cmd, lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

static void row_switch(lv_obj_t *list, const char *title, const char *hint,
                       bool on, luxor_ui_command_t cmd)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = mk_row(list, 54);
    mk_label(r, 14, hint ? 8 : 17, 150, 20, title, f16(), LV_TEXT_ALIGN_LEFT, t->text);
    if (hint) mk_label(r, 14, 30, 160, 16, hint, f14(), LV_TEXT_ALIGN_LEFT, t->muted);

    lv_obj_t *s = lv_switch_create(r);
    size_control(s, LUX_SWITCH_W, LUX_SWITCH_H);
    lv_obj_set_pos(s, LUX_SWITCH_X, LUX_SWITCH_Y);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, col(t->panel2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, col(t->accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, col(t->text), LV_PART_KNOB);
    lv_obj_add_event_cb(s, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)cmd);
}

/* Theme picker, as one row: a swatch showing the current accent, the theme
 * name, and a tap that advances to the next one.  It used to be a whole app,
 * which is a lot of screen for a setting you change twice. */
static void theme_row_cb(lv_event_t *e)
{
    (void)e;
    theme_index = (uint8_t)((theme_index + 1) % THEME_COUNT);
    emit(LUXOR_UI_THEME_SAVE, theme_index);
    rebuild_requested = true;
}

static void row_theme(lv_obj_t *list)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = mk_row(list, LUX_ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, theme_row_cb, LV_EVENT_CLICKED, NULL);

    mk_label(r, 12, 8, 150, 20, "Theme", f16(), LV_TEXT_ALIGN_LEFT, t->text);
    mk_label(r, 12, 28, 150, 16, themes[theme_index].name, f14(),
             LV_TEXT_ALIGN_LEFT, t->muted);

    lv_obj_t *sw = lv_obj_create(r);
    lv_obj_set_size(sw, LUX_SWATCH_D, LUX_SWATCH_D);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sw, col(themes[theme_index].accent), 0);
    lv_obj_set_style_bg_grad_color(sw, col(themes[theme_index].bg0), 0);
    lv_obj_set_style_bg_grad_dir(sw, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_dither_mode(sw, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, col(t->muted), 0);
    lv_obj_set_style_pad_all(sw, 0, 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void row_action(lv_obj_t *list, const char *title, const char *hint,
                       luxor_ui_command_t cmd, int open_app)
{
    const luxor_theme_t *t = th();
    lv_obj_t *r = mk_row(list, 54);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, col(t->accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_40, LV_STATE_PRESSED);
    mk_label(r, 14, hint ? 8 : 17, 180, 20, title, f16(), LV_TEXT_ALIGN_LEFT, t->text);
    if (hint) mk_label(r, 14, 30, 190, 16, hint, f14(), LV_TEXT_ALIGN_LEFT, t->muted);
    mk_label(r, LUX_LIST_W - 40, 18, 22, 20, LV_SYMBOL_RIGHT, f14(), LV_TEXT_ALIGN_RIGHT, t->accent);
    if (open_app >= 0) lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)open_app);
    else if (cmd != LUXOR_UI_NONE)
        lv_obj_add_event_cb(r, cmd_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
}

/* ======================================================= HOME LAUNCHER */

/* One word per tile.  A tile that needs a caption to be understood needs a
 * better word, not a caption. */
static const struct {
    luxor_app_t app;
    const char *name;
} launcher[LUXOR_LAUNCHER_APPS] = {
    { LUXOR_APP_DISPLAY,    "DISPLAY"  },   /* centre     */
    { LUXOR_APP_SETTINGS,   "SETTINGS" },   /* 12 o'clock */
    { LUXOR_APP_MUSIC,      "MUSIC"    },   /*  2         */
    { LUXOR_APP_CALCULATOR, "CALC"     },   /*  4         */
    { LUXOR_APP_CLOCK,      "CLOCK"    },   /*  6         */
    { LUXOR_APP_MAZE,       "MAZE"     },   /*  8         */
    { LUXOR_APP_FITNESS,    "STEPS"    },   /* 10         */
};

/* Largest font / letter spacing for which every launcher name fits
 * LUX_TILE_LABEL_MAX_W.  Measured, not guessed: at Montserrat 16 with 1 px
 * tracking "SETTINGS" is 87 px and ran into the hexagon's slanted edges. */
static void tile_label_style(const lv_font_t **font, int *space)
{
    static const lv_font_t *pf;
    static int ps;
    if (!pf) {
        const lv_font_t *fonts[4]  = { f16(), f16(), f14(), f14() };
        const int        spaces[4] = { 1, 0, 1, 0 };
        pf = fonts[3]; ps = spaces[3];
        for (int k = 0; k < 4; k++) {
            lv_coord_t widest = 0;
            for (int i = 0; i < LUXOR_LAUNCHER_APPS; i++) {
                lv_point_t sz;
                lv_txt_get_size(&sz, launcher[i].name, fonts[k], spaces[k], 0,
                                LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                if (sz.x > widest) widest = sz.x;
            }
            if (widest <= LUX_TILE_LABEL_MAX_W) { pf = fonts[k]; ps = spaces[k]; break; }
        }
    }
    *font = pf;
    *space = ps;
}

static void tile_zoom_exec(void *o, int32_t v) { lv_img_set_zoom((lv_obj_t *)o, (uint16_t)v); }

static void tile_zoom_to(lv_obj_t *tile, int32_t target)
{
    lv_anim_del(tile, tile_zoom_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tile);
    lv_anim_set_values(&a, lv_img_get_zoom(tile), target);
    lv_anim_set_time(&a, 90);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, tile_zoom_exec);
    lv_anim_start(&a);
}

static void tile_press_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *tile = lv_event_get_target(e);
    if (c == LV_EVENT_PRESSED) tile_zoom_to(tile, 243);                 /* 95 % */
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) tile_zoom_to(tile, LV_IMG_ZOOM_NONE);
}

static void add_tile(int slot)
{
    const luxor_theme_t *t = th();
    bool centre = (slot == 0);
    int cx = lux_tiles[slot].cx, cy = lux_tiles[slot].cy;
    lv_obj_t *tile;

    if (hex_ok) {
        tile = lv_img_create(lv_scr_act());
        lv_img_set_src(tile, &hex_dsc[centre ? 1 : 0]);
        lv_obj_set_pos(tile, cx - HEXW / 2, cy - HEXH / 2);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_img_recolor(tile, col(t->accent2), LV_STATE_PRESSED);
        lv_obj_set_style_img_recolor_opa(tile, LV_OPA_30, LV_STATE_PRESSED);
    } else {
        /* Sprite memory was unavailable: rounded tiles keep the watch usable. */
        tile = lv_btn_create(lv_scr_act());
        lv_obj_set_pos(tile, cx - LUX_HEX_W / 2, cy - LUX_HEX_H / 2);
        lv_obj_set_size(tile, LUX_HEX_W, LUX_HEX_H);
        style_button(tile, centre);
        lv_obj_set_style_radius(tile, 20, 0);
    }
    lv_obj_add_event_cb(tile, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)launcher[slot].app);

    /* The sprite is a rectangle and neighbouring sprites overlap at the
     * corners, so a tap in that corner would go to whichever tile was
     * created last.  Hit-test against the actual hexagon instead. */
    lv_obj_add_flag(tile, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(tile, hex_hit_test_cb, LV_EVENT_HIT_TEST, NULL);

    /* Press feedback: the tile tints gold while the finger is on it, so a
     * tap is acknowledged before the screen changes. */
    if (hex_ok) {
        lv_obj_set_style_img_recolor(tile, col(t->accent), LV_STATE_PRESSED);
        lv_obj_set_style_img_recolor_opa(tile, LV_OPA_40, LV_STATE_PRESSED);
        /* ...and sinks to 95 %.  This uses the image widget's own zoom,
         * which the image draw routine applies directly.  (A style
         * transform_zoom goes through a layer buffer instead, and a tile
         * with alpha does not fit LVGL's 24 KB simple layer: in the
         * simulator the pressed tile simply vanished.) */
        lv_img_set_pivot(tile, HEXW / 2, HEXH / 2);
        lv_img_set_antialias(tile, true);
        lv_obj_add_event_cb(tile, tile_press_cb, LV_EVENT_ALL, NULL);
    }

    /* The centre tile is filled with the accent, so its label has to flip to
     * whichever of ink/paper still contrasts with that fill. */
    uint32_t name_col = centre ? (t->light ? LUX_PAPER : LUX_BLACK) : t->text;

    lv_obj_t *n = lv_label_create(tile);
    lv_label_set_text(n, launcher[slot].name);
    const lv_font_t *lf;
    int ls;
    tile_label_style(&lf, &ls);
    lv_obj_set_style_text_font(n, lf, 0);
    lv_obj_set_style_text_color(n, col(name_col), 0);
    lv_obj_set_style_text_letter_space(n, ls, 0);
    lv_obj_center(n);
}

static lux_timebox_t home_time;

/* ------------------------------------------------------ battery badge */

static lv_obj_t *mk_rect(lv_obj_t *parent, int x, int y, int wdt, int hgt)
{
    lv_obj_t *o = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, wdt, hgt);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* "82%" and an outlined battery whose fill tracks the charge.  Built once per
 * home screen; home_update() only touches it when the percentage changes. */
static void add_battery_badge(void)
{
    const luxor_theme_t *t = th();
    const int gx = LUX_BATT_X + LUX_BATT_W - LUX_BATT_NUB_W - LUX_BATT_BODY_W;
    const int gy = LUX_BATT_Y + (LUX_BATT_H - LUX_BATT_BODY_H) / 2;

    w.batt_txt = mk_label(NULL, gx - LUX_BATT_GAP - LUX_BATT_TEXT_W, LUX_BATT_Y + 3,
                          LUX_BATT_TEXT_W, LUX_BATT_H - 3, "--", f14(),
                          LV_TEXT_ALIGN_RIGHT, t->muted);

    w.batt_body = mk_rect(NULL, gx, gy, LUX_BATT_BODY_W, LUX_BATT_BODY_H);
    lv_obj_set_style_bg_opa(w.batt_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w.batt_body, LUX_BATT_STROKE, 0);
    lv_obj_set_style_border_color(w.batt_body, col(t->muted), 0);
    lv_obj_set_style_radius(w.batt_body, 3, 0);

    w.batt_nub = mk_rect(NULL, gx + LUX_BATT_BODY_W, gy + (LUX_BATT_BODY_H - LUX_BATT_NUB_H) / 2,
                         LUX_BATT_NUB_W, LUX_BATT_NUB_H);
    lv_obj_set_style_bg_color(w.batt_nub, col(t->muted), 0);
    lv_obj_set_style_bg_opa(w.batt_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.batt_nub, 1, 0);

    w.batt_fill = mk_rect(NULL, gx + LUX_BATT_STROKE + 1, gy + LUX_BATT_STROKE + 1,
                          1, LUX_BATT_INNER_H);
    lv_obj_set_style_bg_opa(w.batt_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.batt_fill, 1, 0);
    lv_obj_add_flag(w.batt_fill, LV_OBJ_FLAG_HIDDEN);
}

static void battery_badge_update(int percent)
{
    if (!w.batt_txt || percent == w.batt_shown) return;
    w.batt_shown = percent;
    const luxor_theme_t *t = th();

    char b[16];                       /* room for any int, keeps -Wformat-truncation quiet */
    if (percent < 0) snprintf(b, sizeof(b), "--");
    else             snprintf(b, sizeof(b), "%d%%", percent);
    set_text(w.batt_txt, b);

    bool low = percent >= 0 && percent <= LUX_BATT_LOW_PERCENT;
    lv_obj_set_style_text_color(w.batt_txt, col(low ? LUX_LOW : (percent < 0 ? t->muted : t->text)), 0);

    int px = lux_batt_fill_px(percent, LUX_BATT_INNER_W);
    if (px <= 0) {
        lv_obj_add_flag(w.batt_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_width(w.batt_fill, px);
        lv_obj_set_style_bg_color(w.batt_fill, col(low ? LUX_LOW : t->accent), 0);
        lv_obj_clear_flag(w.batt_fill, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_home(void)
{
    screen_base();
    build_hex_sprites();
    const luxor_theme_t *t = th();
    add_edge_ring(LV_OPA_20);

    /* Time above the rosette, pinned to the screen's vertical axis. */
    home_time = mk_timebox(LUX_CX + 4, LUX_HOME_TIME_Y, LUX_HOME_TIME_H,
                           LUX_HOME_TIME_W / 2, f28(), t->text, f14(), t->accent);
    w.l1 = home_time.hh;

    /* Slot 0 is the centre; the outer six are drawn after it so their rims
     * overlap the centre tile rather than the other way round. */
    add_tile(0);
    for (int i = 1; i < LUXOR_LAUNCHER_APPS; i++) add_tile(i);

    w.l2 = mk_label(NULL, LUX_HOME_FOOT_X, LUX_HOME_FOOT_Y, LUX_HOME_FOOT_W, LUX_HOME_FOOT_H,
                    "", f14(), LV_TEXT_ALIGN_CENTER, t->muted);
    lv_obj_set_style_text_letter_space(w.l2, 1, 0);

    add_battery_badge();
}

/* =========================================================== CLOCK APP */

/* A concentric hairline ring, used to build the dial's depth. */
static void dial_ring(int d, uint32_t colour, int width, lv_opa_t opa)
{
    lv_obj_t *r = lv_obj_create(lv_scr_act());
    lv_obj_set_size(r, d, d);
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, width, 0);
    lv_obj_set_style_border_color(r, col(colour), 0);
    lv_obj_set_style_border_opa(r, opa, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static lux_timebox_t clock_time;

static lv_obj_t *mk_hand(int width, uint32_t colour)
{
    lv_obj_t *l = lv_line_create(lv_scr_act());
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, col(colour), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

/* Lay a hand from the pivot to a point on the given radius, at angle
 * `deg` measured clockwise from 12.  The line object is moved to the tight
 * bounding box of the two points so only that box is redrawn. */
static void set_hand(lv_obj_t *l, lv_point_t pts[2], float deg, int r_tip, int width)
{
    float a = (deg - 90.0f) * PI_F / 180.0f;
    int tx = LUX_CX + (int)lroundf(cosf(a) * r_tip);
    int ty = LUX_CY + (int)lroundf(sinf(a) * r_tip);
    int x0 = LV_MIN(LUX_CX, tx) - width, y0 = LV_MIN(LUX_CY, ty) - width;
    pts[0].x = (lv_coord_t)(LUX_CX - x0); pts[0].y = (lv_coord_t)(LUX_CY - y0);
    pts[1].x = (lv_coord_t)(tx - x0);     pts[1].y = (lv_coord_t)(ty - y0);
    lv_obj_set_pos(l, x0, y0);
    lv_line_set_points(l, pts, 2);
}

static void create_clock(void)
{
    screen_base();

    /* The dial is deliberately fixed: white face, black numerals, gold hands.
     * That is what a dress watch looks like, and a launcher theme has no
     * business restyling it.  Only the surround follows the theme. */
    const luxor_theme_t *t = th();
    lv_obj_set_style_bg_color(lv_scr_act(), col(t->light ? 0xE8E3D6 : LUX_BLACK), 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), col(t->light ? 0xD8D1BE : 0x141008), 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(lv_scr_act(), LV_DITHER_ORDERED, 0);

    /* Bezel: a wide gold ring outside the dial, like a case. */
    dial_ring(LUX_BEZEL_D, LUX_GOLD,      6, LV_OPA_COVER);
    dial_ring(LUX_BEZEL_D - 12, LUX_GOLD_DEEP, 2, LV_OPA_70);

    lv_obj_t *m = lv_meter_create(lv_scr_act());
    lv_obj_set_size(m, LUX_DIAL_D, LUX_DIAL_D);
    lv_obj_center(m);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m, col(LUX_PAPER), 0);
    lv_obj_set_style_bg_grad_color(m, col(0xEFEADC), 0);
    lv_obj_set_style_bg_grad_dir(m, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(m, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_set_style_border_color(m, col(LUX_GOLD_DEEP), 0);
    lv_obj_set_style_border_opa(m, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(m, 8, 0);
    /* lv_meter would otherwise print the numeric value beside each major tick. */
    lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    w.dial = m;

    /* 61 ticks over 360 degrees puts one exactly every 6 degrees, with the
     * last overlapping the first.  This is the only way lv_meter spaces ticks
     * evenly, and it is why the old hand-placed dot ring looked slightly off. */
    lv_meter_scale_t *ticks = lv_meter_add_scale(m);
    lv_meter_set_scale_range(m, ticks, 0, 60, 360, 270);
    lv_meter_set_scale_ticks(m, ticks, 61, 2, 8, col(LUX_INK_SOFT));
    lv_meter_set_scale_major_ticks(m, ticks, 5, 4, 16, col(LUX_GOLD_DEEP), 10);

    /* Chapter ring and an inner ring around the wordmark: two hairlines are
     * the difference between a dial and a circle with hands on it. */
    dial_ring(LUX_CHAPTER_D, LUX_GOLD_DEEP, 1, LV_OPA_50);
    dial_ring(LUX_INNER_D,   LUX_GOLD_DEEP, 1, LV_OPA_30);


    /* Twelve Roman numerals, IIII at four o'clock in the horological
     * convention.  Twelve numerals instead of four is most of what separates
     * a watch face from a clock graphic. */
    static const char *roman[12] = {
        "XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"
    };
    for (int i = 0; i < 12; i++) {
        float a = (float)i * PI_F / 6.0f;
        int x = LUX_CX + (int)lroundf(sinf(a) * LUX_NUMERAL_R);
        int y = LUX_CY - (int)lroundf(cosf(a) * LUX_NUMERAL_R);
        lv_obj_t *n = mk_label_c(NULL, x, y, LUX_NUMERAL_W, LUX_NUMERAL_H,
                                 roman[i], (i % 3 == 0) ? f20() : f16(), LUX_INK);
        lv_obj_set_style_text_letter_space(n, 1, 0);
    }

    /* Wordmark. One line: the dial should not need a subtitle either. */
    lv_obj_t *mark = mk_label_c(NULL, LUX_CX, LUX_CY + LUX_MARK_DY, 160, 24,
                                "LUXOR", f20(), LUX_GOLD_DEEP);
    lv_obj_set_style_text_letter_space(mark, 5, 0);

    /* Date window at 3 o'clock, printed rather than drawn: ink on paper with
     * a gold frame, the way a real date aperture reads. */
    lv_obj_t *dbox = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(dbox, LUX_DATE_X, LUX_DATE_Y);
    lv_obj_set_size(dbox, LUX_DATE_W, LUX_DATE_H);
    lv_obj_set_style_radius(dbox, 4, 0);
    lv_obj_set_style_bg_color(dbox, col(LUX_PAPER), 0);
    lv_obj_set_style_bg_grad_color(dbox, col(0xE9E3D3), 0);
    lv_obj_set_style_bg_grad_dir(dbox, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(dbox, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_border_width(dbox, 2, 0);
    lv_obj_set_style_border_color(dbox, col(LUX_GOLD_DEEP), 0);
    lv_obj_set_style_pad_all(dbox, 0, 0);
    lv_obj_clear_flag(dbox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    w.l3 = mk_label(dbox, 0, 4, LUX_DATE_W - 4, 20, "--", f16(),
                    LV_TEXT_ALIGN_CENTER, LUX_INK);

    /* Digital line, colon pinned to the dial axis like everything else. */
    clock_time = mk_timebox(LUX_CX + 2, LUX_CY + LUX_DIGITAL_DY, 24, 78,
                            f20(), LUX_INK, f14(), LUX_GOLD_DEEP);
    w.l1 = clock_time.hh;

    /* Hands last, so they sit above the numerals and the date window.
     *
     * They are lv_line objects, NOT lv_meter needles.  lv_meter invalidates
     * the entire 364x364 dial every time any needle moves, which at 20 Hz
     * for the sweep hand meant re-sending the whole face to the panel twenty
     * times a second and starving touch input -- that was the "janky" feel.
     * A line only invalidates its own bounding box.
     *
     * Each hand is a wide stub plus a narrow tip; drawn together they read
     * as one tapered hand. */
    w.hand_hs = mk_hand(11, LUX_GOLD_DEEP);
    w.hand_h  = mk_hand(5,  LUX_GOLD_DEEP);
    w.hand_ms = mk_hand(8,  LUX_GOLD_DEEP);
    w.hand_m  = mk_hand(4,  LUX_GOLD_DEEP);
    w.hand_s  = mk_hand(2,  0xB4341E);

    /* Centre cap, drawn over the pivot so the three hands meet cleanly. */
    lv_obj_t *cap = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cap, 2 * LUX_HAND_CAP_R, 2 * LUX_HAND_CAP_R);
    lv_obj_center(cap);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, col(LUX_GOLD), 0);
    lv_obj_set_style_bg_grad_color(cap, col(LUX_GOLD_DEEP), 0);
    lv_obj_set_style_bg_grad_dir(cap, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(cap, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_border_width(cap, 2, 0);
    lv_obj_set_style_border_color(cap, col(LUX_INK), 0);
    lv_obj_set_style_border_opa(cap, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* No HOME chip here on purpose.  It would land on top of the XII, and a
     * watch face with a button printed across the twelve is not a watch face.
     * Double-tap the glass, or press either physical button, to get back. */
}

static void clock_update(void)
{
    if (!w.dial) return;
    struct tm tmv;
    localtime_r(&snap.now, &tmv);

    timebox_set(&clock_time, &tmv, snap.clock_24h, snap.time_valid);

    if (!snap.time_valid) {
        set_text(w.l3, "--");
        return;
    }

    int32_t hv = (tmv.tm_hour % 12) * 60 + tmv.tm_min;           /* 0..719   */
    int32_t mv = tmv.tm_min * 60 + tmv.tm_sec;                   /* 0..3599  */
    int32_t sv = snap.sweep_seconds
                     ? tmv.tm_sec * 1000 + (int32_t)(snap.sub_us / 1000)
                     : tmv.tm_sec * 1000;                        /* 0..59999 */
    const int R = LUX_DIAL_D / 2;

    if (hv != w.shown_h) {
        float deg = hv * (360.0f / 720.0f);
        set_hand(w.hand_hs, w.hand_pts[0], deg, R + LUX_HAND_HOUR_STUB, 11);
        set_hand(w.hand_h,  w.hand_pts[1], deg, R + LUX_HAND_HOUR_MOD,  5);
        w.shown_h = hv;
    }
    if (mv != w.shown_m) {
        float deg = mv * (360.0f / 3600.0f);
        set_hand(w.hand_ms, w.hand_pts[2], deg, R + LUX_HAND_MIN_STUB, 8);
        set_hand(w.hand_m,  w.hand_pts[3], deg, R + LUX_HAND_MIN_MOD,  4);
        w.shown_m = mv;
    }
    /* Quantise the sweep to 1/12 s: smooth to the eye, six times cheaper
     * than redrawing on every 50 ms frame. */
    sv = (sv / 83) * 83;
    if (sv != w.shown_s) {
        set_hand(w.hand_s, w.hand_pts[4], sv * (360.0f / 60000.0f), R + LUX_HAND_SEC_MOD, 2);
        w.shown_s = sv;
    }

    char b[8];
    snprintf(b, sizeof(b), "%02d", tmv.tm_mday);
    set_text(w.l3, b);
}

/* ========================================================= FITNESS APP */

/* Ring colours: gold for steps, rose gold for active minutes, platinum for
 * distance.  Fixed across themes, like the metals on a real dial. */
static const uint32_t act_col[LUX_ACT_RINGS] = { 0xD4AF37, 0xC98B7E, 0x9AAFC4 };
static const char *act_unit[LUX_ACT_RINGS]   = { "STEPS", "MIN", "KM" };
static lv_obj_t *act_arc[LUX_ACT_RINGS], *act_val[LUX_ACT_RINGS];

static void create_fitness(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    app_header("STEPS");

    for (int i = 0; i < LUX_ACT_RINGS; i++) {
        int d = lux_act_ring_d(i);
        lv_obj_t *a = lv_arc_create(lv_scr_act());
        lv_obj_set_pos(a, LUX_CX - d / 2, LUX_ACT_CY - d / 2);
        lv_obj_set_size(a, d, d);
        lv_arc_set_rotation(a, 270);            /* start at 12 o'clock */
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_range(a, 0, 1000);           /* per mille: smooth fill */
        lv_arc_set_value(a, 0);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(a, 0, 0);
        lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, LUX_ACT_RING_W, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, col(act_col[i]), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(a, t->light ? LV_OPA_20 : LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, LUX_ACT_RING_W, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, col(act_col[i]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
        act_arc[i] = a;

        int x = lux_act_stat_x(i);
        act_val[i] = mk_label(NULL, x, LUX_ACT_STAT_Y, LUX_ACT_STAT_W, LUX_ACT_VAL_H,
                              "0", f20(), LV_TEXT_ALIGN_CENTER, t->text);
        lv_obj_t *u = mk_label(NULL, x, LUX_ACT_STAT_Y + LUX_ACT_VAL_H + 2, LUX_ACT_STAT_W,
                               LUX_ACT_STAT_H - LUX_ACT_VAL_H - 2, act_unit[i], f14(),
                               LV_TEXT_ALIGN_CENTER, act_col[i]);
        lv_obj_set_style_text_letter_space(u, 2, 0);
    }
    w.arc = act_arc[0];
}

/* Grouped thousands: 12,480 reads at a glance, 12480 has to be parsed. */
static void fmt_thousands(char *b, size_t n, uint32_t v)
{
    if (v >= 1000000) snprintf(b, n, "%" PRIu32 ",%03" PRIu32 ",%03" PRIu32,
                               v / 1000000, (v / 1000) % 1000, v % 1000);
    else if (v >= 1000) snprintf(b, n, "%" PRIu32 ",%03" PRIu32, v / 1000, v % 1000);
    else snprintf(b, n, "%" PRIu32, v);
}

static void fitness_update(void)
{
    if (!w.arc) return;
    float goal_km = (float)snap.goal_steps * (float)snap.stride_cm / 100000.0f;
    unsigned mins = snap.active_seconds / 60;
    float frac[LUX_ACT_RINGS] = {
        snap.goal_steps ? (float)snap.watch.steps / (float)snap.goal_steps : 0.0f,
        (float)mins / (float)LUX_ACTIVE_GOAL_MIN,
        goal_km > 0.0f ? snap.distance_km / goal_km : 0.0f,
    };
    for (int i = 0; i < LUX_ACT_RINGS; i++) {
        int32_t v = (int32_t)lroundf((frac[i] > 1.0f ? 1.0f : frac[i]) * 1000.0f);
        if (v < 0) v = 0;
        if (lv_arc_get_value(act_arc[i]) != v) lv_arc_set_value(act_arc[i], v);
    }
    char b[24];
    fmt_thousands(b, sizeof(b), snap.watch.steps);
    set_text(act_val[0], b);
    snprintf(b, sizeof(b), "%u", mins);
    set_text(act_val[1], b);
    snprintf(b, sizeof(b), "%.2f", (double)snap.distance_km);
    set_text(act_val[2], b);
}

/* ======================================================== TILT MAZE APP */

static float ball_x, ball_y, ball_vx, ball_vy;
static bool maze_solved;
static uint32_t maze_last_ms;

static void maze_reset(void)
{
    ball_x = LUX_MAZE_START_X;
    ball_y = LUX_MAZE_START_Y;
    ball_vx = ball_vy = 0;
    maze_solved = false;
    maze_last_ms = lv_tick_get();
}

static void maze_reset_cb(lv_event_t *e) { (void)e; maze_reset(); luxor_ui_toast("Ball reset"); }

static bool maze_blocked(float x, float y)
{
    float dx = x - LUX_MAZE_CX, dy = y - LUX_MAZE_CY;
    if (sqrtf(dx * dx + dy * dy) > (float)(LUX_MAZE_R - LUX_BALL_R)) return true;
    for (int i = 0; i < LUX_MAZE_WALLS; i++) {
        const lux_wall_t *v = &lux_maze_walls[i];
        if (x + LUX_BALL_R > v->x && x - LUX_BALL_R < v->x + v->w &&
            y + LUX_BALL_R > v->y && y - LUX_BALL_R < v->y + v->h) return true;
    }
    return false;
}

/* Flip the up/down sense of the tilt, right here on the game screen.
 *
 * The axis map is a property of how the sensor is soldered, so the right fix
 * is still the map -- but nobody should have to find a setup page to make a
 * game work.  This toggles LUX_IMU_INV_Y on the stored map, which is exactly
 * what Motion setup's FLIP Y does, so the bubble level and the maze can never
 * disagree.  It persists. */
static void recal_cb(lv_event_t *e)
{
    (void)e;
    emit(LUXOR_UI_TILT_CAL, 0xFF);
    snap.tilt_cal_done = false;
    luxor_ui_toast("Open the maze to calibrate");
}

static void maze_flip_cb(lv_event_t *e)
{
    (void)e;
    uint8_t next = (uint8_t)((snap.imu_map ^ LUX_IMU_INV_Y) & LUX_IMU_MAP_MASK);
    emit(LUXOR_UI_IMU_MAP, next);
    snap.imu_map = next;                     /* reflect it before the next frame */
    maze_reset();
    luxor_ui_toast("Up/down flipped");
}

/* Self-calibration.
 *
 * Nobody should have to know which bit of an axis map is wrong.  The first
 * time the maze opens, it hides the ball and asks for one physical movement
 * whose direction cannot be misread: tip the USB-C edge down towards the
 * floor.  Whatever sign the (mapped) tilt comes back with, the ball must
 * roll DOWN the screen for that movement -- so if it comes back negative,
 * the Y inversion bit is flipped and the corrected map is stored.  It runs
 * once, and Settings -> Calibrate tilt runs it again. */
static bool maze_cal_active;
static int  maze_cal_frames;

static void create_maze(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    app_header("MAZE");
    maze_cal_active = snap.watch.imu_ok && !snap.tilt_cal_done;
    maze_cal_frames = 0;

    lv_obj_t *arena = lv_obj_create(lv_scr_act());
    lv_obj_set_size(arena, 2 * LUX_MAZE_R, 2 * LUX_MAZE_R);
    lv_obj_set_pos(arena, LUX_MAZE_CX - LUX_MAZE_R, LUX_MAZE_CY - LUX_MAZE_R);
    lv_obj_set_style_radius(arena, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(arena, col(t->panel2), 0);
    lv_obj_set_style_bg_opa(arena, t->light ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_border_width(arena, 3, 0);
    lv_obj_set_style_border_color(arena, col(t->accent), 0);
    lv_obj_set_style_border_opa(arena, t->light ? LV_OPA_90 : LV_OPA_50, 0);
    lv_obj_set_style_pad_all(arena, 0, 0);
    lv_obj_clear_flag(arena, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < LUX_MAZE_WALLS; i++) {
        lv_obj_t *v = lv_obj_create(lv_scr_act());
        lv_obj_set_pos(v, lux_maze_walls[i].x, lux_maze_walls[i].y);
        lv_obj_set_size(v, lux_maze_walls[i].w, lux_maze_walls[i].h);
        lv_obj_set_style_radius(v, 4, 0);
        lv_obj_set_style_bg_color(v, col(t->light ? LUX_INK : t->accent), 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(v, 0, 0);
        lv_obj_set_style_pad_all(v, 0, 0);
        lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *g = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(g, LUX_MAZE_GOAL_X - LUX_GOAL_R, LUX_MAZE_GOAL_Y - LUX_GOAL_R);
    lv_obj_set_size(g, 2 * LUX_GOAL_R, 2 * LUX_GOAL_R);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g, col(t->accent2), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g, 2, 0);
    lv_obj_set_style_border_color(g, col(t->accent), 0);
    lv_obj_set_style_pad_all(g, 0, 0);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    w.goal = g;

    lv_obj_t *b = lv_obj_create(lv_scr_act());
    lv_obj_set_size(b, 2 * LUX_BALL_R, 2 * LUX_BALL_R);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    /* The ball has to be the darkest thing on a paper arena and the
     * lightest on an ink one, or it disappears into the floor. */
    lv_obj_set_style_bg_color(b, col(t->light ? LUX_INK : 0xEDF2F8), 0);
    lv_obj_set_style_bg_grad_color(b, col(t->light ? 0x3A3524 : 0xBFC7D2), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(b, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, col(t->accent), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    w.ball = b;

    /* Two controls under the arena: put the ball back, and flip the up/down
     * sense.  Both are glyph-only chips so the screen stays wordless. */
    lv_obj_t *r = mk_round_button(NULL, LUX_CX - LUX_MAZE_BTN_DX, LUX_MAZE_BTN_CY,
                                  LUX_MAZE_BTN_R, LV_SYMBOL_REFRESH, false);
    lv_obj_add_event_cb(r, maze_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fl = mk_round_button(NULL, LUX_CX + LUX_MAZE_BTN_DX, LUX_MAZE_BTN_CY,
                                   LUX_MAZE_BTN_R, LV_SYMBOL_UP LV_SYMBOL_DOWN, false);
    lv_obj_add_event_cb(fl, maze_flip_cb, LV_EVENT_CLICKED, NULL);

    /* Calibration prompt, only while calibrating; sits inside the arena
     * where the ball would be, so nothing is added to the screen. */
    if (maze_cal_active) {
        lv_obj_add_flag(w.ball, LV_OBJ_FLAG_HIDDEN);
        w.l2 = mk_label(NULL, LUX_MAZE_CX - 90, LUX_MAZE_CY - 40, 180, 80,
                        "TILT THE WATCH\nUSB-C EDGE\nTOWARDS THE FLOOR",
                        f16(), LV_TEXT_ALIGN_CENTER, t->text);
        lv_obj_set_style_text_letter_space(w.l2, 1, 0);
        lv_obj_set_style_text_line_space(w.l2, 6, 0);
    } else {
        w.l2 = NULL;
    }
    maze_reset();
}

/* One frame of calibration.  Returns true while still calibrating. */
static bool maze_calibrate_frame(void)
{
    if (!maze_cal_active) return false;
    float ty = snap.tilt_y, tx = snap.tilt_x;
    /* Decisive, mostly-vertical tilt for ~0.4 s; anything else resets. */
    if (fabsf(ty) > 0.30f && fabsf(ty) > 1.5f * fabsf(tx)) maze_cal_frames++;
    else maze_cal_frames = 0;
    if (maze_cal_frames < 8) return true;

    uint8_t map = snap.imu_map;
    if (ty < 0.0f) map = (uint8_t)((map ^ LUX_IMU_INV_Y) & LUX_IMU_MAP_MASK);
    emit(LUXOR_UI_TILT_CAL, map);
    snap.imu_map = map;
    snap.tilt_cal_done = true;
    maze_cal_active = false;
    if (w.l2) { lv_obj_del(w.l2); w.l2 = NULL; }
    lv_obj_clear_flag(w.ball, LV_OBJ_FLAG_HIDDEN);
    maze_reset();
    luxor_ui_toast(ty < 0.0f ? "Calibrated - up/down corrected" : "Calibrated");
    return false;
}

static void maze_update(void)
{
    if (!w.ball) return;

    if (!snap.watch.imu_ok) return;          /* nothing to drive the ball with */
    if (maze_calibrate_frame()) return;
    if (maze_solved) return;

    uint32_t now = lv_tick_get();
    float dt = (float)(now - maze_last_ms) / 1000.0f;
    maze_last_ms = now;
    if (dt < 0.004f || dt > 0.12f) dt = 0.05f;

    /* Plain marble physics: the ball rolls towards whichever edge is lower.
     * tilt_x / tilt_y already point that way (the calibration above pins
     * the sign on this board), so there is no sign handling here at all. */
    float ax = snap.tilt_x * 1150.0f;
    float ay = snap.tilt_y * 1150.0f;
    float damp = expf(-dt / 0.85f);
    ball_vx = (ball_vx + ax * dt) * damp;
    ball_vy = (ball_vy + ay * dt) * damp;

    float speed = sqrtf(ball_vx * ball_vx + ball_vy * ball_vy);
    if (speed > 280.0f) { ball_vx *= 280.0f / speed; ball_vy *= 280.0f / speed; }

    float nx = ball_x + ball_vx * dt;
    if (!maze_blocked(nx, ball_y)) ball_x = nx; else ball_vx *= -0.32f;
    float ny = ball_y + ball_vy * dt;
    if (!maze_blocked(ball_x, ny)) ball_y = ny; else ball_vy *= -0.32f;

    lv_obj_set_pos(w.ball, (int)lroundf(ball_x) - LUX_BALL_R, (int)lroundf(ball_y) - LUX_BALL_R);

    float dx = ball_x - LUX_MAZE_GOAL_X, dy = ball_y - LUX_MAZE_GOAL_Y;
    if (sqrtf(dx * dx + dy * dy) < LUX_GOAL_R) {
        maze_solved = true;
        luxor_ui_toast("Solved");
    }
}

/* ============================================================ MUSIC APP */

static lv_obj_t *eq_bar[LUX_EQ_BARS];

static void create_music(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    app_header("MUSIC");

    lv_obj_t *disc = lv_obj_create(lv_scr_act());
    lv_obj_set_size(disc, LUX_DISC_D, LUX_DISC_D);
    lv_obj_set_pos(disc, LUX_CX - LUX_DISC_D / 2, LUX_DISC_CY - LUX_DISC_D / 2);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, col(t->panel), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 4, 0);
    lv_obj_set_style_border_color(disc, col(t->accent), 0);
    lv_obj_set_style_pad_all(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* A five-bar level meter instead of a label: it breathes while a track
     * plays and rests low when paused, so the state is visible at a glance. */
    for (int i = 0; i < LUX_EQ_BARS; i++) {
        lv_obj_t *b = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LUX_EQ_W, LUX_EQ_MIN_H);
        lv_obj_set_pos(b, lux_eq_x(i), LUX_DISC_CY - LUX_EQ_MIN_H / 2);
        lv_obj_set_style_radius(b, LUX_EQ_W / 2, 0);
        lv_obj_set_style_bg_color(b, col(t->accent), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        eq_bar[i] = b;
    }
    w.l1 = mk_label(NULL, LUX_CX - 120, LUX_MUSIC_TITLE_Y, 240, 24,
                    music_names[0], f20(), LV_TEXT_ALIGN_CENTER, t->text);

    struct { const char *sym; luxor_ui_command_t cmd; bool accent; } keys[3] = {
        { LV_SYMBOL_PREV,  LUXOR_UI_MUSIC_PREV,   false },
        { LV_SYMBOL_PLAY,  LUXOR_UI_MUSIC_TOGGLE, true  },
        { LV_SYMBOL_NEXT,  LUXOR_UI_MUSIC_NEXT,   false },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = mk_button(NULL, lux_row_item_x(i, 3, LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_GAP),
                                LUX_MUSIC_BTN_Y, LUX_MUSIC_BTN_W, LUX_MUSIC_BTN_H,
                                keys[i].sym, f20(), keys[i].accent, keys[i].cmd, 0);
        if (i == 1) w.l2 = lv_obj_get_child(b, 0);
    }
    w.l4 = mk_label(NULL, LUX_CX - LUX_MUSIC_FOOT_W / 2, LUX_MUSIC_FOOT_Y,
                    LUX_MUSIC_FOOT_W, LUX_MUSIC_FOOT_H, "", f14(), LV_TEXT_ALIGN_CENTER, t->muted);
}

static void music_update(void)
{
    if (snap.music_track < MUSIC_TRACKS) set_text(w.l1, music_names[snap.music_track]);
    set_text(w.l2, snap.music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    /* Bar heights: a sum of two slow sines per bar with different phases,
     * which reads as music without pretending to be a spectrum analyser. */
    uint32_t ms = lv_tick_get();
    for (int i = 0; i < LUX_EQ_BARS; i++) {
        if (!eq_bar[i]) continue;
        int hgt = LUX_EQ_MIN_H;
        if (snap.music_playing) {
            float ph = (float)ms * 0.001f;
            float v = 0.5f + 0.30f * sinf(ph * (5.1f + i * 1.3f) + i * 1.7f)
                           + 0.20f * sinf(ph * (8.3f - i * 0.9f) + i * 0.6f);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            hgt = LUX_EQ_MIN_H + (int)lroundf(v * (LUX_EQ_MAX_H - LUX_EQ_MIN_H));
        }
        if (lv_obj_get_height(eq_bar[i]) != hgt) {
            lv_obj_set_height(eq_bar[i], hgt);
            lv_obj_set_y(eq_bar[i], LUX_DISC_CY - hgt / 2);
        }
    }
    char b[40];
    snprintf(b, sizeof(b), "%u / %d  \xC2\xB7  VOL %u%%",
             (unsigned)(snap.music_track + 1), MUSIC_TRACKS, snap.volume);
    set_text(w.l4, b);
}

/* ======================================================= CALCULATOR APP */

static char calc_text[24] = "0";
static double calc_acc;
static char calc_op;
static bool calc_new_entry = true;

typedef enum { K_NONE = 0, K_C, K_BACK, K_DIV, K_MUL, K_SUB, K_ADD, K_EQ, K_DOT, K_SIGN, K_D0 } calc_key_t;

static double calc_current(void) { return strtod(calc_text, NULL); }

static void calc_format(double v)
{
    if (!isfinite(v)) { snprintf(calc_text, sizeof(calc_text), "Error"); calc_new_entry = true; return; }
    snprintf(calc_text, sizeof(calc_text), "%.9g", v);
    calc_new_entry = true;
}

static void calc_apply(void)
{
    double v = calc_current();
    if (!calc_op) { calc_acc = v; calc_new_entry = true; return; }
    switch (calc_op) {
    case '+': calc_acc += v; break;
    case '-': calc_acc -= v; break;
    case '*': calc_acc *= v; break;
    case '/':
        if (fabs(v) < 1e-12) {
            snprintf(calc_text, sizeof(calc_text), "Error");
            calc_op = 0; calc_new_entry = true;
            return;
        }
        calc_acc /= v;
        break;
    default: break;
    }
    calc_format(calc_acc);
}

static void calc_digit(char ch)
{
    if (calc_new_entry || strcmp(calc_text, "Error") == 0) {
        if (ch == '.') snprintf(calc_text, sizeof(calc_text), "0.");
        else { calc_text[0] = ch; calc_text[1] = 0; }
        calc_new_entry = false;
        return;
    }
    size_t n = strlen(calc_text);
    if (n >= sizeof(calc_text) - 2) return;
    if (ch == '.' && strchr(calc_text, '.')) return;
    if (n == 1 && calc_text[0] == '0' && ch != '.') { calc_text[0] = ch; return; }
    calc_text[n] = ch;
    calc_text[n + 1] = 0;
}

/* The display uses Inter Display Light at 48 px, like a watch's big
 * numerals, and steps down to 28 px only when the number would not fit. */
static void calc_show(void)
{
    if (!w.calc_disp) return;
    lv_point_t sz;
    lv_txt_get_size(&sz, calc_text, f48(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *want = sz.x <= LUX_CALC_DISP_W - 40 ? f48() : f28();
    if (lv_obj_get_style_text_font(w.calc_disp, 0) != want) {
        lv_obj_set_style_text_font(w.calc_disp, want, 0);
        lv_obj_set_y(w.calc_disp, want == f48() ? 2 : 12);
        lv_obj_set_height(w.calc_disp, want == f48() ? 54 : 34);
    }
    set_text(w.calc_disp, calc_text);
}

static void calc_cb(lv_event_t *e)
{
    intptr_t k = (intptr_t)lv_event_get_user_data(e);
    if (k >= K_D0) { calc_digit((char)('0' + (k - K_D0))); }
    else switch (k) {
    case K_C:    strcpy(calc_text, "0"); calc_acc = 0; calc_op = 0; calc_new_entry = true; break;
    case K_BACK: {
        size_t n = strlen(calc_text);
        if (!calc_new_entry && n > 1) calc_text[n - 1] = 0;
        else { strcpy(calc_text, "0"); calc_new_entry = true; }
        break;
    }
    case K_SIGN: {
        double v = -calc_current();
        bool was_new = calc_new_entry;
        calc_format(v);
        calc_new_entry = was_new;
        break;
    }
    case K_DOT:  calc_digit('.'); break;
    case K_EQ:   calc_apply(); calc_op = 0; break;
    case K_ADD: case K_SUB: case K_MUL: case K_DIV:
        calc_apply();
        calc_op = k == K_ADD ? '+' : k == K_SUB ? '-' : k == K_MUL ? '*' : '/';
        calc_new_entry = true;
        break;
    default: break;
    }
    calc_show();
    if (w.l1) {
        char b[8] = "";
        if (calc_op) snprintf(b, sizeof(b), "%s", calc_op == '*' ? "\xC3\x97"        /* × */
                                              : calc_op == '/' ? "\xC3\xB7"        /* ÷ */
                                              : calc_op == '-' ? "\xE2\x88\x92"    /* − */
                                              : "+");
        set_text(w.l1, b);
    }
}

static void create_calculator(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    add_home_chip();

    lv_obj_t *frame = mk_card(LUX_CALC_DISP_X - 8, LUX_CALC_DISP_Y - 6,
                              LUX_CALC_DISP_W + 16, LUX_CALC_DISP_H + 12);
    lv_obj_set_style_radius(frame, 16, 0);
    w.l1 = mk_label(frame, 10, 16, 24, 24, "", f20(), LV_TEXT_ALIGN_LEFT, t->accent);
    w.calc_disp = mk_label(frame, 32, 12, LUX_CALC_DISP_W - 32, 34,
                           calc_text, f28(), LV_TEXT_ALIGN_RIGHT, t->text);
    calc_show();

    static const struct { const char *txt; calc_key_t key; bool accent; } keys[19] = {
        { "C",  K_C,       false }, { LV_SYMBOL_BACKSPACE, K_BACK, false },
        { "\xC3\xB7", K_DIV, true }, { "\xC3\x97", K_MUL, true },
        { "7",  K_D0 + 7,  false }, { "8",  K_D0 + 8,    false },
        { "9",  K_D0 + 9,  false }, { "\xE2\x88\x92", K_SUB, true },
        { "4",  K_D0 + 4,  false }, { "5",  K_D0 + 5,    false },
        { "6",  K_D0 + 6,  false }, { "+",  K_ADD,       true  },
        { "1",  K_D0 + 1,  false }, { "2",  K_D0 + 2,    false },
        { "3",  K_D0 + 3,  false }, { "=",  K_EQ,        true  },
        { "0",  K_D0 + 0,  false }, { ".",  K_DOT,       false },
        { "\xC2\xB1", K_SIGN,  false },          /* ± */
    };

    int k = 0;
    for (int r = 0; r < LUX_CALC_ROWS; r++) {
        for (int c = 0; c < lux_calc_cols(r) && k < 19; c++, k++) {
            lv_obj_t *b = lv_btn_create(lv_scr_act());
            lv_obj_set_pos(b, lux_calc_key_x(r, c), lux_calc_row_y(r));
            size_control(b, LUX_CALC_KEY_W, LUX_CALC_KEY_H);
            style_button(b, keys[k].accent);
            lv_obj_add_event_cb(b, calc_cb, LV_EVENT_CLICKED, (void *)(intptr_t)keys[k].key);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text(l, keys[k].txt);
            lv_obj_set_style_text_font(l, f20(), 0);
            lv_obj_set_style_text_color(l, col(keys[k].accent ? on_accent() : t->text), 0);
            lv_obj_center(l);
        }
    }
}

/* ========================================================== THEMES APP */

/* ======================================================== DISPLAY APP
 *
 * This screen exists to show a picture, so the picture gets the middle of the
 * glass and everything else is caption-sized.  The plates are drawn here
 * rather than stored as bitmaps: six 252x252 images would be 762 kB of flash,
 * where the drawing code is a couple of kilobytes and can be re-rendered in
 * any palette.  They are original geometric compositions -- no bundled
 * photographs, no licensed artwork.
 */

#define ARTD   LUX_ART_D
static uint8_t     *art_buf;
static lv_img_dsc_t art_dsc;
static bool         art_ok;
static int          art_drawn = -1;
static int          art_drawn_theme = -1;
static uint8_t      art_index;

static void art_line(lv_obj_t *cv, float x0, float y0, float x1, float y1,
                     uint32_t colour, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col(colour);
    d.width = (lv_coord_t)width;
    d.opa   = opa;
    d.round_start = d.round_end = 1;
    lv_point_t p[2] = {
        { (lv_coord_t)lroundf(x0), (lv_coord_t)lroundf(y0) },
        { (lv_coord_t)lroundf(x1), (lv_coord_t)lroundf(y1) },
    };
    lv_canvas_draw_line(cv, p, 2, &d);
}

/* A polyline in one draw call.  Dune and Meridian used to issue one call
 * per 3 px segment -- about 1,400 of them -- which froze the screen for
 * over a second every time the Display app opened. */
static void art_poly(lv_obj_t *cv, const lv_point_t *pts, uint32_t n,
                     uint32_t colour, int width, lv_opa_t opa)
{
    if (n < 2) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col(colour);
    d.width = (lv_coord_t)width;
    d.opa   = opa;
    d.round_start = d.round_end = 1;
    lv_canvas_draw_line(cv, pts, n, &d);
}

static void art_ring(lv_obj_t *cv, float r, uint32_t colour, int width, lv_opa_t opa)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = col(colour);
    d.width = (lv_coord_t)width;
    d.opa   = opa;
    lv_canvas_draw_arc(cv, ARTD / 2, ARTD / 2, (lv_coord_t)lroundf(r), 0, 360, &d);
}

static void art_disc(lv_obj_t *cv, float cx, float cy, float r, uint32_t colour, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color  = col(colour);
    d.bg_opa    = opa;
    d.radius    = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, (lv_coord_t)lroundf(cx - r), (lv_coord_t)lroundf(cy - r),
                        (lv_coord_t)lroundf(2 * r), (lv_coord_t)lroundf(2 * r), &d);
}

/* Six plates.  Each is built from the same three inks -- paper, ink, gold --
 * so the set reads as one collection rather than six unrelated pictures. */
static void draw_art_plate(lv_obj_t *cv, int which, const luxor_theme_t *t)
{
    const float c = ARTD / 2.0f;
    const float R = ARTD / 2.0f - 2.0f;
    uint32_t paper = t->light ? LUX_PAPER : 0x0C0C0C;
    uint32_t ink   = t->light ? LUX_INK   : LUX_WHITE;
    uint32_t gold  = t->light ? LUX_GOLD_DEEP : LUX_GOLD;

    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    art_disc(cv, c, c, R, paper, LV_OPA_COVER);

    switch (which) {
    case 0:                                   /* Sunburst */
        for (int i = 0; i < 72; i++) {
            float a = (float)i * PI_F / 36.0f;
            float inner = (i % 6 == 0) ? 22.0f : (i % 2 ? 58.0f : 40.0f);
            art_line(cv, c + cosf(a) * inner, c + sinf(a) * inner,
                     c + cosf(a) * (R - 10), c + sinf(a) * (R - 10),
                     (i % 6 == 0) ? gold : ink, (i % 6 == 0) ? 3 : 1,
                     (i % 2) ? LV_OPA_40 : LV_OPA_80);
        }
        art_disc(cv, c, c, 24, gold, LV_OPA_COVER);
        art_ring(cv, R - 5, gold, 2, LV_OPA_80);
        break;

    case 1:                                   /* Orbit */
        for (int i = 0; i < 5; i++) {
            art_ring(cv, R - 12 - i * 21.0f, ink, 1, LV_OPA_40);
        }
        for (int i = 0; i < 5; i++) {
            float a = (float)i * 1.17f + 0.4f;
            float rr = R - 12 - i * 21.0f;
            art_disc(cv, c + cosf(a) * rr, c + sinf(a) * rr,
                     4.0f + i, (i == 2) ? gold : ink, LV_OPA_COVER);
        }
        art_disc(cv, c, c, 19, gold, LV_OPA_COVER);
        art_ring(cv, 30, gold, 1, LV_OPA_60);
        art_ring(cv, R - 4, gold, 3, LV_OPA_90);
        break;

    case 2:                                   /* Guilloche */
        for (int i = 0; i < 44; i++) {
            float a = (float)i * 2.0f * PI_F / 44.0f;
            float rr = R - 16;
            /* Chords stepped by a constant offset trace the cardioid-like
             * envelope that engine-turned watch dials are famous for. */
            int j = (i * 17) % 44;
            float b = (float)j * 2.0f * PI_F / 44.0f;
            art_line(cv, c + cosf(a) * rr, c + sinf(a) * rr,
                     c + cosf(b) * rr, c + sinf(b) * rr,
                     (i % 4 == 0) ? gold : ink, 1,
                     (i % 4 == 0) ? LV_OPA_70 : LV_OPA_30);
        }
        art_ring(cv, R - 10, gold, 2, LV_OPA_80);
        art_disc(cv, c, c, 14, gold, LV_OPA_COVER);
        break;

    case 3: {                                 /* Dune */
        static lv_point_t pts[96];
        for (int row = 0; row < 7; row++) {
            float base = c - R * 0.55f + row * (R * 0.26f);
            float amp  = 9.0f - row * 0.6f;
            /* Clip the row to the plate, leaving room for the wave. */
            float dy   = fabsf(base - c) + amp;
            float lim  = (R - 10) * (R - 10) - dy * dy;
            if (lim <= 0) continue;
            float half = sqrtf(lim);
            uint32_t n = 0;
            for (float x = -half; x <= half && n < 96; x += 4.0f) {
                float yy = base + sinf(x / (26.0f - row * 2.0f) + row) * amp;
                pts[n].x = (lv_coord_t)lroundf(c + x);
                pts[n].y = (lv_coord_t)lroundf(yy);
                n++;
            }
            art_poly(cv, pts, n, (row == 3) ? gold : ink,
                     (row == 3) ? 3 : 2, (row == 3) ? LV_OPA_90 : LV_OPA_50);
        }
        art_disc(cv, c + R * 0.34f, c - R * 0.46f, 17, gold, LV_OPA_COVER);
        art_ring(cv, R - 4, gold, 2, LV_OPA_70);
        break;
    }

    case 4:                                   /* Atrium -- an art-deco fan */
        for (int i = 0; i <= 18; i++) {
            float a = PI_F + (float)i * PI_F / 18.0f;
            art_line(cv, c, c + R * 0.62f,
                     c + cosf(a) * (R - 12), c + R * 0.62f + sinf(a) * (R * 0.98f),
                     (i % 3 == 0) ? gold : ink, (i % 3 == 0) ? 3 : 1,
                     (i % 3 == 0) ? LV_OPA_80 : LV_OPA_40);
        }
        for (int i = 1; i <= 4; i++) {
            lv_draw_arc_dsc_t d;
            lv_draw_arc_dsc_init(&d);
            d.color = col(i == 2 ? gold : ink);
            d.width = i == 2 ? 3 : 1;
            d.opa   = i == 2 ? LV_OPA_80 : LV_OPA_40;
            lv_canvas_draw_arc(cv, ARTD / 2, (lv_coord_t)lroundf(c + R * 0.62f),
                               (lv_coord_t)lroundf(i * (R * 0.24f)), 180, 360, &d);
        }
        art_ring(cv, R - 4, gold, 2, LV_OPA_70);
        break;

    default:                                  /* Meridian -- a globe grid */
        for (int i = 1; i < 7; i++) {
            float yy = c - R + i * (2 * R / 7.0f);
            float half = sqrtf(fmaxf(0.0f, (R - 8) * (R - 8) - (yy - c) * (yy - c)));
            art_line(cv, c - half, yy, c + half, yy,
                     (i == 3) ? gold : ink, (i == 3) ? 3 : 1,
                     (i == 3) ? LV_OPA_90 : LV_OPA_30);
        }
        for (int i = 1; i < 7; i++) {
            static lv_point_t mp[41];
            float k = (float)(i - 3) / 3.0f;
            for (int seg = 0; seg <= 40; seg++) {
                float t = -PI_F / 2 + (float)seg * PI_F / 40.0f;
                mp[seg].x = (lv_coord_t)lroundf(c + k * cosf(t) * (R - 8));
                mp[seg].y = (lv_coord_t)lroundf(c + sinf(t) * (R - 8));
            }
            art_poly(cv, mp, 41, ink, 1, LV_OPA_30);
        }
        art_disc(cv, c, c, 13, gold, LV_OPA_COVER);
        art_ring(cv, R - 4, gold, 2, LV_OPA_80);
        break;
    }
}

static void build_art_plate(void)
{
    if (art_ok && art_drawn == (int)art_index && art_drawn_theme == (int)theme_index) return;

    size_t sz = (size_t)LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(ARTD, ARTD);
    if (!art_buf) {
        art_buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!art_buf) art_buf = heap_caps_malloc(sz, MALLOC_CAP_DEFAULT);
        if (!art_buf) { art_ok = false; return; }
    }

    lv_obj_t *cv = lv_canvas_create(lv_scr_act());
    lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_buffer(cv, art_buf, ARTD, ARTD, LV_IMG_CF_TRUE_COLOR_ALPHA);
    draw_art_plate(cv, art_index % LUX_ART_COUNT, th());
    lv_obj_del(cv);

    art_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    art_dsc.header.always_zero = 0;
    art_dsc.header.reserved = 0;
    art_dsc.header.w = ARTD;
    art_dsc.header.h = ARTD;
    art_dsc.data_size = sz;
    art_dsc.data = art_buf;
    lv_img_cache_invalidate_src(NULL);
    art_ok = true;
    art_drawn = (int)art_index;
    art_drawn_theme = (int)theme_index;
}

static void art_step(int delta)
{
    art_index = (uint8_t)((art_index + LUX_ART_COUNT + delta) % LUX_ART_COUNT);
    emit(LUXOR_UI_ART_SAVE, art_index);
    rebuild_requested = true;
}

static void art_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t d = lv_indev_get_gesture_dir(indev);
    /* Swiping left drags the next plate in from the right, which is the way
     * every phone gallery behaves. */
    if (d == LV_DIR_LEFT)       art_step(+1);
    else if (d == LV_DIR_RIGHT) art_step(-1);
}

static void art_arrow_cb(lv_event_t *e) { art_step((int)(intptr_t)lv_event_get_user_data(e)); }

static void create_display(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    add_home_chip();          /* no masthead: the plate's name is the caption */
    build_art_plate();

    lv_obj_add_flag(lv_scr_act(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lv_scr_act(), art_gesture, LV_EVENT_GESTURE, NULL);

    /* The plate sits in a thin gold frame -- a mount, not a border. */
    lv_obj_t *frame = lv_obj_create(lv_scr_act());
    lv_obj_set_size(frame, LUX_ART_D + 2 * LUX_ART_FRAME, LUX_ART_D + 2 * LUX_ART_FRAME);
    lv_obj_set_pos(frame, LUX_ART_X - LUX_ART_FRAME, LUX_ART_Y - LUX_ART_FRAME);
    lv_obj_set_style_radius(frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(frame, col(t->panel), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, col(t->accent), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    if (art_ok) {
        w.art = lv_img_create(lv_scr_act());
        lv_img_set_src(w.art, &art_dsc);
        lv_obj_set_pos(w.art, LUX_ART_X, LUX_ART_Y);
        lv_obj_clear_flag(w.art, LV_OBJ_FLAG_CLICKABLE);
    } else {
        /* No spare PSRAM for a canvas: say so rather than showing a hole. */
        mk_label_c(NULL, LUX_CX, LUX_ART_CY, 200, 24, "no memory for artwork",
                   f16(), t->muted);
    }

    lv_obj_t *nm = mk_label(NULL, LUX_CX - LUX_ART_NAME_W / 2, LUX_ART_NAME_Y,
                            LUX_ART_NAME_W, LUX_ART_NAME_H,
                            art_names[art_index % LUX_ART_COUNT], f20(),
                            LV_TEXT_ALIGN_CENTER, t->text);
    lv_obj_set_style_text_letter_space(nm, 2, 0);

    for (int i = 0; i < LUX_ART_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(lv_scr_act());
        lv_obj_set_pos(d, lux_row_item_x(i, LUX_ART_COUNT, LUX_DOT_D, LUX_DOT_GAP), LUX_DOT_Y);
        lv_obj_set_size(d, LUX_DOT_D, LUX_DOT_D);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, col(i == art_index ? t->accent : t->muted), 0);
        lv_obj_set_style_bg_opa(d, i == art_index ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *prev = mk_round_button(NULL, LUX_CX - LUX_ARROW_DX, LUX_ARROW_CY, LUX_ARROW_R,
                                     LV_SYMBOL_LEFT, false);
    lv_obj_add_event_cb(prev, art_arrow_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *next = mk_round_button(NULL, LUX_CX + LUX_ARROW_DX, LUX_ARROW_CY, LUX_ARROW_R,
                                     LV_SYMBOL_RIGHT, false);
    lv_obj_add_event_cb(next, art_arrow_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
}

/* ======================================================== SETTINGS APP */

static void create_settings(void)
{
    screen_base();
    stepper_count = 0;
    app_header("SETTINGS");

    lv_obj_t *l = mk_list();
    row_slider(l, "Brightness", 5, 100, (int)snap.brightness, LUXOR_UI_BRIGHTNESS);
    row_slider(l, "Volume", 0, 100, (int)snap.volume, LUXOR_UI_VOLUME);
    row_theme(l);
    row_switch(l, "24-hour clock", NULL, snap.clock_24h, LUXOR_UI_CLOCK_24H);
    row_switch(l, "Sweeping seconds", "smoother, more power",
               snap.sweep_seconds, LUXOR_UI_SWEEP_SECONDS);
    row_action(l, "Set time", "hour, minute and date", LUXOR_UI_NONE, LUXOR_APP_TIMESET);
    row_action(l, "Sync time over Wi-Fi", "network set in menuconfig",
               LUXOR_UI_WIFI_TIME, -1);
    row_action(l, "Calibrate tilt", "one tilt, then done", LUXOR_UI_NONE, -1);
    lv_obj_add_event_cb(lv_obj_get_child(l, -1), recal_cb, LV_EVENT_CLICKED, NULL);
    row_action(l, "Motion setup", "axis map and gyroscope", LUXOR_UI_NONE, LUXOR_APP_MOTION);
    row_stepper(l, "Daily step goal", LUXOR_UI_STEP_GOAL, (int32_t)snap.goal_steps,
                500, 1000, 40000, "%d steps", false);
    row_stepper(l, "Stride length", LUXOR_UI_STRIDE, (int32_t)snap.stride_cm,
                1, 30, 150, "%d cm", false);
    row_stepper(l, "Screen timeout", LUXOR_UI_SLEEP_TIMEOUT, (int32_t)snap.sleep_s,
                10, 10, 180, "%d s", false);
    row_action(l, "Reset today's steps", NULL, LUXOR_UI_RESET_STEPS, -1);
    row_action(l, "About Luxor", "firmware " LUXOR_FW_VERSION, LUXOR_UI_NONE, LUXOR_APP_ABOUT);
}

/* ======================================================== SET TIME APP */

void luxor_ui_time_edit_seed(time_t local_now)
{
    struct tm tmv;
    localtime_r(&local_now, &tmv);
    if (tmv.tm_year < 124) { tmv.tm_year = 126; tmv.tm_mon = 0; tmv.tm_mday = 1; }
    edit_year = tmv.tm_year + 1900;
    edit_mon  = tmv.tm_mon + 1;
    edit_day  = tmv.tm_mday;
    edit_hour = tmv.tm_hour;
    edit_min  = tmv.tm_min;
}

void luxor_ui_time_edit_get(struct tm *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->tm_year = edit_year - 1900;
    out->tm_mon  = edit_mon - 1;
    out->tm_mday = edit_day;
    out->tm_hour = edit_hour;
    out->tm_min  = edit_min;
    out->tm_sec  = 0;
    out->tm_isdst = -1;      /* let the C library decide about daylight saving */
}

static void create_timeset(void)
{
    screen_base();
    stepper_count = 0;
    app_header("TIME");

    luxor_ui_time_edit_seed(snap.now);

    lv_obj_t *l = mk_list();
    /* Order matters: stepper_cb reads indices 0..4 when clamping the date. */
    row_stepper(l, "Year",   LUXOR_UI_NONE, edit_year, 1, 2024, 2099, "%d",    true);
    row_stepper(l, "Month",  LUXOR_UI_NONE, edit_mon,  1, 1,    12,   "%d",    true);
    row_stepper(l, "Day",    LUXOR_UI_NONE, edit_day,  1, 1,
                days_in_month(edit_year, edit_mon), "%d", true);
    row_stepper(l, "Hour",   LUXOR_UI_NONE, edit_hour, 1, 0,    23,   "%02d",  true);
    row_stepper(l, "Minute", LUXOR_UI_NONE, edit_min,  1, 0,    59,   "%02d",  true);

    lv_obj_t *r = mk_row(l, 54);
    lv_obj_t *save = lv_btn_create(r);
    lv_obj_set_pos(save, 20, 8);
    size_control(save, LUX_LIST_W - 48, 38);
    style_button(save, true);
    lv_obj_add_event_cb(save, cmd_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)LUXOR_UI_TIME_SAVE);
    lv_obj_t *sl = lv_label_create(save);
    lv_label_set_text(sl, LV_SYMBOL_OK "  SAVE TIME");
    lv_obj_set_style_text_color(sl, col(on_accent()), 0);
    lv_obj_set_style_text_font(sl, f16(), 0);
    lv_obj_center(sl);
}

/* ================================================== MOTION / IMU SETUP */

static void axis_cb(lv_event_t *e)
{
    uint8_t bit = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    uint8_t map = (uint8_t)(snap.imu_map ^ bit) & LUX_IMU_MAP_MASK;
    snap.imu_map = map;                    /* so a second tap toggles again */
    emit(LUXOR_UI_IMU_MAP, map);
    rebuild_requested = true;
}

static void create_motion(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    app_header("MOTION");

    lv_obj_t *ring = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ring, 2 * LUX_LEVEL_R, 2 * LUX_LEVEL_R);
    lv_obj_set_pos(ring, LUX_CX - LUX_LEVEL_R, LUX_LEVEL_CY - LUX_LEVEL_R);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, col(t->panel), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, col(t->accent), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *cross = lv_obj_create(ring);
    lv_obj_set_size(cross, 26, 26);
    lv_obj_center(cross);
    lv_obj_set_style_radius(cross, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cross, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cross, 1, 0);
    lv_obj_set_style_border_color(cross, col(t->muted), 0);
    lv_obj_set_style_border_opa(cross, LV_OPA_50, 0);
    lv_obj_clear_flag(cross, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *dot = lv_obj_create(lv_scr_act());
    lv_obj_set_size(dot, 2 * LUX_LEVEL_DOT_R, 2 * LUX_LEVEL_DOT_R);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, col(t->accent), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    w.dot = dot;

    struct { const char *txt; uint8_t bit; } axes[3] = {
        { "FLIP X", LUX_IMU_INV_X },
        { "FLIP Y", LUX_IMU_INV_Y },
        { "SWAP",   LUX_IMU_SWAP_XY },
    };
    for (int i = 0; i < 3; i++) {
        bool on = (snap.imu_map & axes[i].bit) != 0;
        lv_obj_t *b = lv_btn_create(lv_scr_act());
        lv_obj_set_pos(b, lux_row_item_x(i, 3, LUX_AXIS_BTN_W, LUX_AXIS_BTN_GAP), LUX_AXIS_BTN_Y);
        size_control(b, LUX_AXIS_BTN_W, LUX_AXIS_BTN_H);
        style_button(b, on);
        lv_obj_add_event_cb(b, axis_cb, LV_EVENT_CLICKED, (void *)(intptr_t)axes[i].bit);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, axes[i].txt);
        lv_obj_set_style_text_font(l, f14(), 0);
        lv_obj_set_style_text_color(l, col(on ? on_accent() : t->text), 0);
        lv_obj_center(l);
    }

    mk_button(NULL, LUX_CX - LUX_ZERO_BTN_W / 2, LUX_ZERO_BTN_Y, LUX_ZERO_BTN_W, LUX_ZERO_BTN_H,
              "ZERO GYRO", f14(), false, LUXOR_UI_GYRO_ZERO, 0);

    w.l1 = mk_label(NULL, LUX_CX - LUX_MOTION_STAT_W / 2, LUX_MOTION_STAT_Y,
                    LUX_MOTION_STAT_W, LUX_MOTION_STAT_H, "", f14(), LV_TEXT_ALIGN_CENTER, t->muted);
}

static void motion_update_screen(void)
{
    if (!w.dot) return;
    float x = snap.tilt_x, y = snap.tilt_y;
    float r = sqrtf(x * x + y * y);
    if (r > 1.0f) { x /= r; y /= r; }
    int travel = LUX_LEVEL_R - LUX_LEVEL_DOT_R - 4;
    int px = LUX_CX + (int)lroundf(x * travel);
    int py = LUX_LEVEL_CY + (int)lroundf(y * travel);
    lv_obj_set_pos(w.dot, px - LUX_LEVEL_DOT_R, py - LUX_LEVEL_DOT_R);

    char b[72];
    if (!snap.watch.imu_ok) snprintf(b, sizeof(b), "IMU not detected");
    else snprintf(b, sizeof(b), "map 0x%X  %+.2f %+.2f %+.2f",
                  snap.imu_map, (double)snap.raw[0], (double)snap.raw[1], (double)snap.raw[2]);
    set_text(w.l1, b);
}

/* ============================================================== ABOUT */

static void create_about(void)
{
    screen_base();
    const luxor_theme_t *t = th();
    app_header("ABOUT");

    lv_obj_t *l = mk_list();
    const char *lines[] = {
        "Firmware", "Luxor " LUXOR_FW_VERSION,
        "1 tap", "wakes a dark screen",
        "2 taps", "on empty space: home",
        "3 taps", "on empty space: off / on",
        "Power", "hold side button 3 s: off",
    };
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i += 2) {
        lv_obj_t *r = mk_row(l, 54);
        mk_label(r, 14, 9, 200, 20, lines[i], f16(), LV_TEXT_ALIGN_LEFT, t->accent);
        mk_label(r, 14, 30, LUX_LIST_W - 30, 18, lines[i + 1], f14(), LV_TEXT_ALIGN_LEFT, t->muted);
    }
    lv_obj_t *r = mk_row(l, 54);
    w.l1 = mk_label(r, 14, 9, 210, 20, "Status", f16(), LV_TEXT_ALIGN_LEFT, t->accent);
    w.l2 = mk_label(r, 14, 30, LUX_LIST_W - 30, 18, "", f14(), LV_TEXT_ALIGN_LEFT, t->muted);
    /* Compare this against a multimeter on the cell to set
     * CONFIG_RW_ADC_SCALE_PERMILLE (menuconfig -> LUXOR). */
    r = mk_row(l, 54);
    mk_label(r, 14, 9, 210, 20, "Battery", f16(), LV_TEXT_ALIGN_LEFT, t->accent);
    w.l3 = mk_label(r, 14, 30, LUX_LIST_W - 30, 18, "", f14(), LV_TEXT_ALIGN_LEFT, t->muted);
}

static void about_update(void)
{
    char b[80];
    snprintf(b, sizeof(b), "touch %s - imu %s - rtc %s",
             snap.touch_ok ? "ok" : "--",
             snap.watch.imu_ok ? "ok" : "--",
             snap.watch.rtc_ok ? "ok" : "--");
    set_text(w.l2, b);
    if (snap.battery_percent >= 0)
        snprintf(b, sizeof(b), "%.2f V filtered - %d %%", (double)snap.battery_v, snap.battery_percent);
    else
        snprintf(b, sizeof(b), "not measured (ADC unavailable)");
    set_text(w.l3, b);
}

/* ============================================================== TOASTS */

static void service_toast(void)
{
    uint32_t now = lv_tick_get();
    bool want = toast_text[0] && (int32_t)(toast_until - now) > 0;

    if (!want) {
        if (w.toast) { lv_obj_del(w.toast); w.toast = NULL; }
        if (!want && toast_text[0] && (int32_t)(toast_until - now) <= 0) toast_text[0] = 0;
        return;
    }
    if (!w.toast) {
        const luxor_theme_t *t = th();
        w.toast = lv_obj_create(lv_scr_act());
        /* Centred, like a phone's volume HUD: it is transient by design, so
         * it may sit over content; a bar at y = 306 sat on top of buttons. */
        lv_obj_set_size(w.toast, 250, 44);
        lv_obj_set_pos(w.toast, LUX_CX - 125, LUX_CY - 22);
        lv_obj_set_style_radius(w.toast, 20, 0);
        lv_obj_set_style_bg_color(w.toast, col(t->accent), 0);
        lv_obj_set_style_bg_opa(w.toast, LV_OPA_90, 0);
        lv_obj_set_style_border_width(w.toast, 0, 0);
        lv_obj_set_style_pad_all(w.toast, 0, 0);
        lv_obj_clear_flag(w.toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *l = mk_label(w.toast, 8, 13, 234, 20, toast_text, f14(),
                               LV_TEXT_ALIGN_CENTER, on_accent());
        lv_obj_set_user_data(w.toast, l);
    } else {
        set_text((lv_obj_t *)lv_obj_get_user_data(w.toast), toast_text);
    }
}

/* ========================================================= MOTION */

/* Screen changes dissolve in from the theme's field colour rather than
 * cutting: a curtain on the top layer fades from opaque to clear over
 * LUX_FADE_MS.  Only its background opacity animates, so each frame is a
 * plain blend -- no layer buffers.  It is not clickable, so a tap during the
 * fade still reaches the new screen. */
#define LUX_FADE_MS        200
#define LUX_SPLASH_HOLD_MS 900
#define LUX_SPLASH_FADE_MS 450

static lv_obj_t *curtain;
static lv_obj_t *curtain_word, *curtain_rule;

static void curtain_exec(void *o, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
    if (curtain_word) lv_obj_set_style_text_opa(curtain_word, (lv_opa_t)v, 0);
    if (curtain_rule) lv_obj_set_style_bg_opa(curtain_rule, (lv_opa_t)v, 0);
}

static void curtain_done(lv_anim_t *a)
{
    lv_obj_del((lv_obj_t *)a->var);
    curtain = curtain_word = curtain_rule = NULL;
}

static void curtain_start(uint32_t colour, uint32_t hold_ms, uint32_t fade_ms)
{
    if (curtain) { lv_anim_del(curtain, NULL); lv_obj_del(curtain); }
    curtain = curtain_word = curtain_rule = NULL;
    curtain = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(curtain);
    lv_obj_set_size(curtain, LUX_W, LUX_H);
    lv_obj_set_style_bg_color(curtain, col(colour), 0);
    lv_obj_set_style_bg_opa(curtain, LV_OPA_COVER, 0);
    lv_obj_clear_flag(curtain, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, curtain);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, fade_ms);
    lv_anim_set_delay(&a, hold_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, curtain_exec);
    lv_anim_set_ready_cb(&a, curtain_done);
    lv_anim_start(&a);
}

/* Boot: the wordmark on black, then the watch dissolves in underneath. */
static void splash_start(void)
{
    curtain_start(LUX_BLACK, LUX_SPLASH_HOLD_MS, LUX_SPLASH_FADE_MS);
    curtain_word = lv_label_create(curtain);
    lv_label_set_text(curtain_word, "LUXOR");
    lv_obj_set_style_text_font(curtain_word, f28(), 0);
    lv_obj_set_style_text_color(curtain_word, col(LUX_GOLD), 0);
    lv_obj_set_style_text_letter_space(curtain_word, 10, 0);
    lv_obj_align(curtain_word, LV_ALIGN_CENTER, 5, -8);   /* +5: trailing tracking */

    curtain_rule = lv_obj_create(curtain);
    lv_obj_remove_style_all(curtain_rule);
    lv_obj_set_size(curtain_rule, 64, 1);
    lv_obj_set_style_bg_color(curtain_rule, col(LUX_GOLD), 0);
    lv_obj_set_style_bg_opa(curtain_rule, LV_OPA_COVER, 0);
    lv_obj_align(curtain_rule, LV_ALIGN_CENTER, 0, 18);
}

/* ========================================================= PUBLIC API */

void luxor_ui_init(uint8_t initial_theme)
{
    theme_index = initial_theme % THEME_COUNT;
    art_index   = 0;
    cmd_head = cmd_tail = 0;
    snap.brightness = 60;
    snap.volume = 45;
    snap.goal_steps = 8000;
    snap.stride_cm = 75;
    snap.sleep_s = 30;
    snap.sweep_seconds = true;
    snap.battery_percent = -1;
    snap.imu_map = LUX_IMU_SWAP_XY;
    luxor_ui_open(LUXOR_APP_HOME);
    splash_start();
}

void luxor_ui_open(luxor_app_t app)
{
    current_app = app;
    switch (app) {
    case LUXOR_APP_DISPLAY:    create_display();    break;
    case LUXOR_APP_SETTINGS:   create_settings();   break;
    case LUXOR_APP_MUSIC:      create_music();      break;
    case LUXOR_APP_CALCULATOR: create_calculator(); break;
    case LUXOR_APP_CLOCK:      create_clock();      break;
    case LUXOR_APP_MAZE:       create_maze();       break;
    case LUXOR_APP_FITNESS:    create_fitness();    break;
    case LUXOR_APP_TIMESET:    create_timeset();    break;
    case LUXOR_APP_MOTION:     create_motion();     break;
    case LUXOR_APP_ABOUT:      create_about();      break;
    case LUXOR_APP_HOME:
    default:                   create_home(); current_app = LUXOR_APP_HOME; break;
    }
    luxor_ui_render(&snap);
    if (!curtain) curtain_start(th()->bg0, 0, LUX_FADE_MS);   /* never cut short the splash */
}

void luxor_ui_home(void)
{
    requested_app = -1;
    if (current_app != LUXOR_APP_HOME) luxor_ui_open(LUXOR_APP_HOME);
}

luxor_app_t luxor_ui_current_app(void) { return current_app; }
uint8_t luxor_ui_theme_index(void) { return theme_index; }

bool luxor_ui_is_dynamic(void)
{
    return current_app == LUXOR_APP_CLOCK ||
           current_app == LUXOR_APP_MAZE ||
           current_app == LUXOR_APP_MOTION ||
           (current_app == LUXOR_APP_MUSIC && snap.music_playing);   /* level meter */
}

void luxor_ui_service(void)
{
    service_toast();
    if (rebuild_requested) { rebuild_requested = false; luxor_ui_open(current_app); return; }
    if (requested_app >= 0) {
        int a = requested_app;
        requested_app = -1;
        luxor_ui_open((luxor_app_t)a);
    }
}

static void home_update(void)
{
    char b[64];
    struct tm tmv;
    localtime_r(&snap.now, &tmv);

    timebox_set(&home_time, &tmv, snap.clock_24h, snap.time_valid);

    if (!snap.time_valid) {
        snprintf(b, sizeof(b), "SET TIME IN SETTINGS");
    } else {
        strftime(b, sizeof(b), "%a %d %b", &tmv);
        for (char *p = b; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    }
    set_text(w.l2, b);
    battery_badge_update(snap.battery_percent);
}

void luxor_ui_render(const luxor_ui_snapshot_t *s)
{
    static bool seeded;
    if (s && s != &snap) snap = *s;
    if (!seeded && s) {
        /* The stored gallery choice arrives with the first snapshot, after
         * luxor_ui_init() has already drawn the home screen. */
        seeded = true;
        if (snap.art_index < LUX_ART_COUNT && snap.art_index != art_index) {
            art_index = snap.art_index;
            if (current_app == LUXOR_APP_DISPLAY) rebuild_requested = true;
        }
    }
    switch (current_app) {
    case LUXOR_APP_HOME:       home_update();          break;
    case LUXOR_APP_CLOCK:      clock_update();         break;
    case LUXOR_APP_FITNESS:    fitness_update();       break;
    case LUXOR_APP_MAZE:       maze_update();          break;
    case LUXOR_APP_MUSIC:      music_update();         break;
    case LUXOR_APP_MOTION:     motion_update_screen(); break;
    case LUXOR_APP_ABOUT:      about_update();         break;
    default: break;
    }
}
