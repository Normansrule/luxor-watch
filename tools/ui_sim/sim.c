/*
 * Luxor UI simulator — runs the REAL main/ui.c on a PC, no watch needed.
 *
 *   ./luxor_sim render <theme> <app> <battery%> <out.raw> [pressed_x pressed_y]
 *   ./luxor_sim check                      every app x every theme: text fit, missing glyphs,
 *                                          44 px touch areas, no overlapping touch areas
 *
 * render writes a 412x412 RGB565 frame (little-endian); montage.py turns
 * frames into a PNG contact sheet.  check walks every label on every screen
 * and fails if its text is wider than the box it has to live in -- the
 * clipped "% OF 8000 STEP GOA" kind of bug that the geometry tests in
 * tests/ cannot see because they know nothing about fonts.
 */
#include "lvgl.h"
#include "ui.h"
#include "layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

watch_state_t g_state;
portMUX_TYPE  g_state_mux;

static lv_color_t fb[412 * 412];
static lv_color_t buf[412 * 412];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * 412 + x] = *p++;
    lv_disp_flush_ready(d);
}

static void sim_init(void)
{
    lv_init();
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, buf, NULL, 412 * 412);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = 412; dd.ver_res = 412; dd.flush_cb = flush; dd.draw_buf = &db;
    lv_disp_drv_register(&dd);
}

static luxor_ui_snapshot_t sample(int theme, int battery)
{
    luxor_ui_snapshot_t s;
    memset(&s, 0, sizeof s);
    struct tm t = { 0 };
    t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 30;
    t.tm_hour = 10; t.tm_min = 9; t.tm_sec = 30; t.tm_isdst = -1;
    s.now = mktime(&t);
    s.time_valid = s.time_trusted = true;
    s.touch_ok = true;
    s.watch.imu_ok = s.watch.rtc_ok = true;
    s.watch.steps = 6240;
    s.goal_steps = 8000;
    s.goal_percent = 78;
    s.distance_km = 4.68f;
    s.active_seconds = 21 * 60;
    s.brightness = 60; s.volume = 45; s.stride_cm = 75; s.sleep_s = 30;
    s.theme_index = (uint8_t)theme;
    s.sweep_seconds = true;
    s.battery_percent = battery;
    s.battery_v = 3.97f;
    return s;
}

static void settle(luxor_ui_snapshot_t *s, int frames)
{
    for (int i = 0; i < frames; i++) {
        luxor_ui_render(s);
        luxor_ui_service();
        lv_tick_inc(20);
        lv_timer_handler();
    }
}

/* ---------------------------------------------------------------- check */

static int overflow_count;

/* Report each (screen, text) once, not once per theme. */
static bool seen(const char *where, const char *txt)
{
    static char keys[256][96];
    static int n;
    char k[96];
    snprintf(k, sizeof k, "%.8s|%.80s", where, txt);
    for (int i = 0; i < n; i++) if (strcmp(keys[i], k) == 0) return true;
    if (n < 256) snprintf(keys[n++], sizeof keys[0], "%s", k);
    return false;
}

static void audit(lv_obj_t *o, const char *where)
{
    if (lv_obj_check_type(o, &lv_label_class) && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        const char *txt = lv_label_get_text(o);
        if (txt && *txt) {
            const lv_font_t *f = lv_obj_get_style_text_font(o, LV_PART_MAIN);
            lv_coord_t ls = lv_obj_get_style_text_letter_space(o, LV_PART_MAIN);
            lv_point_t sz;
            lv_txt_get_size(&sz, txt, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            int own_w = lv_area_get_width(&a);
            int room = own_w;
            /* A content-sized label inside a button must fit the button. */
            lv_obj_t *p = lv_obj_get_parent(o);
            if (p && lv_obj_get_parent(p)) {
                lv_area_t pa;
                lv_obj_get_coords(p, &pa);
                /* Centred content may use the button's padding, not its rim. */
                int pw = lv_area_get_width(&pa) - 2 * (lv_obj_get_style_border_width(p, 0) + 2);
                if (lv_obj_check_type(p, &lv_btn_class) && pw < room) room = pw;
            }
            /* Every character must exist in the label's font. */
            for (uint32_t i = 0; txt[i];) {
                uint32_t cp = _lv_txt_encoded_next(txt, &i);
                lv_font_glyph_dsc_t g;
                if (cp >= 0x20 && !lv_font_get_glyph_dsc(f, &g, cp, 0) && !seen(where, txt)) {
                    printf("MISSING  %-10s U+%04X in \"%s\"\n", where, (unsigned)cp, txt);
                    overflow_count++;
                }
            }
            /* Multi-line labels wrap; only single-line text can overflow. */
            if (!strchr(txt, '\n') && sz.x > room + 1 && !seen(where, txt)) {
                printf("OVERFLOW %-10s \"%s\" needs %d px, has %d\n", where, txt, sz.x, room);
                overflow_count++;
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) audit(lv_obj_get_child(o, i), where);
}

/* ---------------------------------------------------------- touch audit */
/* Every clickable object must offer at least LUX_HIT_MIN x LUX_HIT_MIN of
 * touch area (drawn size + extended click area), and two controls that are
 * not nested may not share touch area, unless they do their own hit test
 * (the hexagon tiles). */
#define MAX_HITS 160
static lv_obj_t *hit_obj[MAX_HITS];
static lv_area_t hit_area[MAX_HITS];
static int hit_n, touch_problems;

static void collect_hits(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (o != lv_scr_act() && lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && hit_n < MAX_HITS) {
        hit_obj[hit_n] = o;
        lv_obj_get_click_area(o, &hit_area[hit_n]);
        hit_n++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) collect_hits(lv_obj_get_child(o, i));
}

static bool is_ancestor(lv_obj_t *a, lv_obj_t *b)
{
    for (lv_obj_t *p = lv_obj_get_parent(b); p; p = lv_obj_get_parent(p)) if (p == a) return true;
    return false;
}

static void touch_audit(const char *where)
{
    hit_n = 0;
    collect_hits(lv_scr_act());
    for (int i = 0; i < hit_n; i++) {
        int w = lv_area_get_width(&hit_area[i]), h = lv_area_get_height(&hit_area[i]);
        lv_area_t c; lv_obj_get_coords(hit_obj[i], &c);
        char key[48];
        snprintf(key, sizeof key, "small %d,%d", c.x1, c.y1);
        if ((w < LUX_HIT_MIN || h < LUX_HIT_MIN) && !seen(where, key)) {
            printf("SMALL    %-10s control at (%d,%d) %dx%d drawn, %dx%d touch\n", where,
                   c.x1, c.y1, lv_area_get_width(&c), lv_area_get_height(&c), w, h);
            touch_problems++;
        }
    }
    for (int i = 0; i < hit_n; i++)
        for (int j = i + 1; j < hit_n; j++) {
            if (is_ancestor(hit_obj[i], hit_obj[j]) || is_ancestor(hit_obj[j], hit_obj[i])) continue;
            if (lv_obj_has_flag(hit_obj[i], LV_OBJ_FLAG_ADV_HITTEST) &&
                lv_obj_has_flag(hit_obj[j], LV_OBJ_FLAG_ADV_HITTEST)) continue;
            lv_area_t x;
            if (_lv_area_intersect(&x, &hit_area[i], &hit_area[j]) &&
                lv_area_get_width(&x) > 1 && lv_area_get_height(&x) > 1) {
                char key[48];
                snprintf(key, sizeof key, "overlap %d,%d", x.x1, x.y1);
                if (seen(where, key)) continue;
                printf("OVERLAP  %-10s touch areas overlap by %dx%d at (%d,%d)\n", where,
                       lv_area_get_width(&x), lv_area_get_height(&x), x.x1, x.y1);
                touch_problems++;
            }
        }
}

static const char *app_name(int a)
{
    static const char *n[] = { "HOME", "DISPLAY", "SETTINGS", "MUSIC", "CALC", "CLOCK",
                               "MAZE", "STEPS", "TIME", "MOTION", "ABOUT" };
    return a >= 0 && a < (int)(sizeof n / sizeof n[0]) ? n[a] : "?";
}

int main(int argc, char **argv)
{
    sim_init();
    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        for (int theme = 0; theme < 6; theme++) {
            luxor_ui_init((uint8_t)theme);
            for (int app = 0; app < LUXOR_APP_COUNT; app++) {
                luxor_ui_snapshot_t s = sample(theme, 100);
                luxor_ui_open((luxor_app_t)app);
                settle(&s, 5);
                char where[32];
                snprintf(where, sizeof where, "%s", app_name(app));
                audit(lv_scr_act(), where);
                touch_audit(where);
            }
        }
        if (overflow_count) printf("\n%d LABEL PROBLEM(S)\n", overflow_count);
        if (touch_problems) printf("\n%d TOUCH PROBLEM(S)\n", touch_problems);
        if (overflow_count || touch_problems) return 1;
        printf("ALL LABELS FIT, ALL GLYPHS PRESENT, EVERY CONTROL HAS A %d PX TOUCH AREA "
               "(%d screens x 6 themes)\n", LUX_HIT_MIN, LUXOR_APP_COUNT);
        return 0;
    }
    if (argc < 6 || strcmp(argv[1], "render") != 0) {
        fprintf(stderr, "usage: %s render <theme> <app> <battery%%> <out.raw> [px py]\n"
                        "       %s check\n", argv[0], argv[0]);
        return 2;
    }
    int theme = atoi(argv[2]), app = atoi(argv[3]), batt = atoi(argv[4]);
    luxor_ui_init((uint8_t)theme);
    luxor_ui_snapshot_t s = sample(theme, batt);
    luxor_ui_open((luxor_app_t)app);
    settle(&s, 100);                   /* 2 s: past the boot splash and every fade */
    if (argc >= 8) {                   /* show the pressed state of whatever is at (px,py) */
        lv_point_t p = { (lv_coord_t)atoi(argv[6]), (lv_coord_t)atoi(argv[7]) };
        lv_obj_t *o = lv_indev_search_obj(lv_scr_act(), &p);
        while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
        if (o) { lv_obj_add_state(o, LV_STATE_PRESSED); lv_event_send(o, LV_EVENT_PRESSED, NULL); }
        settle(&s, 20);
    }
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    FILE *f = fopen(argv[5], "wb");
    if (!f) return 1;
    fwrite(fb, 2, 412 * 412, f);
    fclose(f);
    return 0;
}
