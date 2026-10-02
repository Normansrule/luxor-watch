/*
 * Host check for the battery gauge (main/battery.c).
 *
 *   make battery
 */
#include "battery.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures, mark;

static void check(int ok, const char *what)
{
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

static void section(const char *name)
{
    if (failures == mark) printf("PASS: %s\n", name);
    mark = failures;
}

/* Deterministic noise in +/- amp. */
static unsigned seed = 12345;
static float noise(float amp)
{
    seed = seed * 1103515245u + 12345u;
    return amp * (((seed >> 8) & 0xFFFF) / 32767.5f - 1.0f);
}

int main(void)
{
    /* ------------------------------------------------------------ curve */
    check(lux_batt_curve(3.0f) == 0.0f, "below cutoff is 0 %");
    check(lux_batt_curve(4.35f) == 100.0f, "above full is 100 %");
    check(fabsf(lux_batt_curve(3.75f) - 40.0f) < 0.01f, "3.75 V is 40 %");
    check(lux_batt_curve(NAN) == 0.0f, "NaN does not propagate");
    {
        float prev = -1.0f;
        int mono = 1;
        for (float v = 3.2f; v <= 4.3f; v += 0.005f) {
            float p = lux_batt_curve(v);
            if (p < prev) mono = 0;
            prev = p;
        }
        check(mono, "curve never decreases with voltage");
    }
    section("voltage curve is monotonic and clamped");

    /* ------------------------------------------------------ first read */
    lux_batt_t b;
    lux_batt_init(&b);
    check(lux_batt_percent(&b) == -1, "unknown before any read");
    lux_batt_update(&b, 3.95f, 1.0f);
    check(lux_batt_percent(&b) == 75, "first good read shows at once");
    section("first read is shown immediately");

    /* ------------------------------------------------------- stability */
    int lo = 101, hi = -1, changes = 0, last = lux_batt_percent(&b);
    for (int s = 0; s < 600; s++) {                 /* 10 min at 1 Hz, +/-40 mV noise */
        lux_batt_update(&b, 3.95f + noise(0.04f), 1.0f);
        int p = lux_batt_percent(&b);
        if (p < lo) lo = p;
        if (p > hi) hi = p;
        if (p != last) changes++;
        last = p;
    }
    printf("   10 min of +/-40 mV noise at 75 %%: shown %d..%d, %d changes\n", lo, hi, changes);
    check(hi - lo <= 2, "noise moves the reading by at most 2 points");
    check(changes <= 4, "the number does not flicker");
    section("noisy readings give a calm number");

    /* ---------------------------------------------------- load steps */
    lux_batt_init(&b);
    lux_batt_update(&b, 3.90f, 1.0f);
    int before = lux_batt_percent(&b);
    lux_batt_update(&b, 3.80f, 1.0f);               /* backlight on: 100 mV sag, 1 s */
    lux_batt_update(&b, 3.90f, 1.0f);
    check(abs(lux_batt_percent(&b) - before) <= 2, "a 1 s load sag barely moves the badge");
    section("brief load sags are filtered");

    /* ------------------------------------------------ real discharge */
    lux_batt_init(&b);
    int mono = 1, prevp = 101;
    for (int s = 0; s <= 3600; s++) {               /* 4.15 V -> 3.40 V over an hour */
        float v = 4.15f - 0.75f * s / 3600.0f + noise(0.01f);
        lux_batt_update(&b, v, 1.0f);
        int p = lux_batt_percent(&b);
        if (p > prevp + 1) mono = 0;                 /* tolerate the dead band, not a climb */
        prevp = p;
    }
    printf("   after an hour's discharge to 3.40 V: %d %%\n", lux_batt_percent(&b));
    check(mono, "discharging never makes the number climb");
    check(lux_batt_percent(&b) <= 8, "a nearly flat cell reads nearly empty");
    section("slow discharge tracks down smoothly");

    /* ------------------------------------------------------ bad reads */
    lux_batt_init(&b);
    lux_batt_update(&b, 3.95f, 1.0f);
    lux_batt_update(&b, -1.0f, 1.0f);
    lux_batt_update(&b, -1.0f, 1.0f);
    check(lux_batt_percent(&b) == 75, "two failed reads keep the last value");
    lux_batt_update(&b, -1.0f, 1.0f);
    check(lux_batt_percent(&b) == -1, "three failed reads in a row -> unknown");
    lux_batt_update(&b, 7.0f, 1.0f);
    check(lux_batt_percent(&b) == -1, "impossible voltages are rejected");
    lux_batt_update(&b, 4.20f, 1.0f);
    check(lux_batt_percent(&b) == 100, "recovers on the next good read");
    section("glitches do not blank the badge");

    /* ----------------------------------------------------- glyph fill */
    check(lux_batt_fill_px(-1, 18) == 0, "unknown -> empty glyph");
    check(lux_batt_fill_px(0, 18) == 0, "0 % -> empty glyph");
    check(lux_batt_fill_px(1, 18) == 1, "1 % still shows a sliver");
    check(lux_batt_fill_px(50, 18) == 9, "50 % -> half");
    check(lux_batt_fill_px(100, 18) == 18, "100 % -> full");
    section("battery glyph fill");

    if (failures) { printf("\n%d BATTERY CHECK(S) FAILED\n", failures); return 1; }
    printf("\nALL BATTERY CHECKS PASSED\n");
    return 0;
}
