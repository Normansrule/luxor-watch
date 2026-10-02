#include "battery.h"

#include <math.h>

void lux_batt_init(lux_batt_t *b)
{
    b->v_filt = 0.0f;
    b->shown  = -1;
    b->bad    = 0;
    b->primed = false;
}

float lux_batt_curve(float v)
{
    /* Coarse single-cell LiPo discharge curve at light load.  Piecewise
     * linear so it is honest at the ends; unchanged from v6's board.c. */
    static const float mv[] = { 3.30f, 3.60f, 3.75f, 3.95f, 4.20f };
    static const float pc[] = { 0.0f, 12.0f, 40.0f, 75.0f, 100.0f };
    if (!(v > mv[0])) return 0.0f;           /* also catches NaN */
    for (int i = 1; i < 5; i++) {
        if (v <= mv[i]) {
            float f = (v - mv[i - 1]) / (mv[i] - mv[i - 1]);
            return pc[i - 1] + f * (pc[i] - pc[i - 1]);
        }
    }
    return 100.0f;
}

void lux_batt_update(lux_batt_t *b, float v, float dt_s)
{
    /* A 3:1 divider on a single cell cannot legitimately read outside this. */
    if (!(v > 2.5f && v < 5.0f)) {
        if (++b->bad >= LUX_BATT_BAD_LIMIT) {
            b->primed = false;
            b->shown  = -1;
        }
        return;
    }
    b->bad = 0;

    if (!b->primed) {
        /* First good read: show it straight away, no slow ramp from zero. */
        b->v_filt = v;
        b->primed = true;
        b->shown  = (int)lroundf(lux_batt_curve(v));
        return;
    }

    if (dt_s < 0.0f) dt_s = 0.0f;
    float a = dt_s / (LUX_BATT_TAU_S + dt_s);
    b->v_filt += a * (v - b->v_filt);

    float p = lux_batt_curve(b->v_filt);
    /* Dead band: only move when the estimate is a full point away. */
    if (p >= (float)b->shown + 1.0f || p <= (float)b->shown - 1.0f)
        b->shown = (int)lroundf(p);
    /* The ends are always reachable, so a full battery reads 100, not 99. */
    if (p >= 99.5f) b->shown = 100;
    if (p <= 0.5f)  b->shown = 0;
}

int lux_batt_percent(const lux_batt_t *b)
{
    return b->primed ? b->shown : -1;
}

float lux_batt_volts(const lux_batt_t *b)
{
    return b->primed ? b->v_filt : -1.0f;
}

int lux_batt_fill_px(int percent, int inner_w)
{
    if (percent <= 0 || inner_w <= 0) return 0;
    if (percent >= 100) return inner_w;
    int px = (percent * inner_w + 50) / 100;
    return px < 1 ? 1 : px;
}
