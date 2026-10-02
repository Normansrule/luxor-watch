/*
 * Original, deliberately simple motion estimator.  Not a validated medical or
 * clinical pedometer - it is a hobby step counter and a tilt source for the
 * user interface.
 */
#include "motion.h"

#include <math.h>
#include <string.h>

#define RAD_TO_DEG 57.295779513f

/* Step detector constants, in g on the accelerometer magnitude. */
#define STEP_THRESHOLD   0.085f    /* peak that counts as a foot strike       */
#define STEP_REARM       0.030f    /* must fall this far below zero to re-arm */
#define STEP_MIN_GAP_MS  270u      /* ~3.7 steps/s ceiling                    */
#define STEP_MAX_GAP_MS  2200u     /* slower than this and the cadence resets */

/* Wrist raise. */
#define RAISE_FACE_UP    0.72f
#define RAISE_COOLDOWN   4000u
#define RAISE_MOTION_MS  1500u

static float lp_alpha(float dt, float tau)
{
    /* Exact one-pole coefficient, so the filter behaves the same whatever the
     * sample interval happens to be on any given loop. */
    return 1.0f - expf(-dt / tau);
}

void motion_init(motion_t *m, uint32_t steps)
{
    memset(m, 0, sizeof(*m));
    m->gravity_mag = 1.0f;
    m->gz_f = 1.0f;
    m->steps = steps;
    m->armed = true;
}

bool motion_is_still(const imu_sample_t *s)
{
    if (!s) return false;
    float mag = sqrtf(s->ax * s->ax + s->ay * s->ay + s->az * s->az);
    return fabsf(mag - 1.0f) < 0.10f &&
           fabsf(s->gx) < 8.0f && fabsf(s->gy) < 8.0f && fabsf(s->gz) < 8.0f;
}

bool motion_update(motion_t *m, const imu_sample_t *s, float dt,
                   uint32_t now_ms, const float gyro_bias[3])
{
    if (!m || !s) return false;
    if (!isfinite(dt) || dt <= 0.0f || dt > 0.2f) return false;
    if (!isfinite(s->ax) || !isfinite(s->ay) || !isfinite(s->az)) return false;
    if (!isfinite(s->gx) || !isfinite(s->gy) || !isfinite(s->gz)) return false;

    float mag = sqrtf(s->ax * s->ax + s->ay * s->ay + s->az * s->az);
    if (mag < 0.1f || mag > 16.0f) return false;

    if (!m->initialized) {
        m->gx_f = s->ax; m->gy_f = s->ay; m->gz_f = s->az;
        m->gravity_mag = mag;
        m->initialized = true;
    }

    /* ---- orientation ---------------------------------------------------
     * A 250 ms low pass on the accelerometer is enough for tilt control and
     * has no drift at all.  The previous build integrated the gyroscope into
     * roll and pitch, which slowly walked the maze ball sideways while the
     * watch sat still; that path is gone. */
    float a = lp_alpha(dt, 0.25f);
    m->gx_f += a * (s->ax - m->gx_f);
    m->gy_f += a * (s->ay - m->gy_f);
    m->gz_f += a * (s->az - m->gz_f);

    float n = sqrtf(m->gx_f * m->gx_f + m->gy_f * m->gy_f + m->gz_f * m->gz_f);
    float ux = 0, uy = 0, uz = 1;
    if (n > 0.05f) { ux = m->gx_f / n; uy = m->gy_f / n; uz = m->gz_f / n; }

    /* ux/uy point along the *up* direction expressed in the watch frame, so a
     * ball rolls towards -ux / -uy.  Screen y is inverted relative to the
     * watch frame's Y, which turns -uy back into +uy. */
    m->tilt_x = -ux;
    m->tilt_y =  uy;
    if (m->tilt_x >  1.0f) m->tilt_x =  1.0f;
    if (m->tilt_x < -1.0f) m->tilt_x = -1.0f;
    if (m->tilt_y >  1.0f) m->tilt_y =  1.0f;
    if (m->tilt_y < -1.0f) m->tilt_y = -1.0f;
    m->roll_deg  = asinf(m->tilt_x) * RAD_TO_DEG;
    m->pitch_deg = asinf(m->tilt_y) * RAD_TO_DEG;

    float bx = gyro_bias ? gyro_bias[0] : 0.0f;
    float by = gyro_bias ? gyro_bias[1] : 0.0f;
    float bz = gyro_bias ? gyro_bias[2] : 0.0f;
    (void)bx; (void)by;
    m->yaw_deg = remainderf(m->yaw_deg + (s->gz - bz) * dt, 360.0f);

    /* ---- step estimation ------------------------------------------------
     * Band pass the accelerometer magnitude: a slow follower removes gravity,
     * a fast follower removes sensor hiss.  A step is a rising crossing of the
     * threshold, and the detector must first see the signal swing negative
     * again, so a single slow tilt cannot ratchet the counter upwards. */
    m->gravity_mag += lp_alpha(dt, 0.75f) * (mag - m->gravity_mag);
    float hp = mag - m->gravity_mag;
    m->band += lp_alpha(dt, 0.05f) * (hp - m->band);

    float gyro_mag = sqrtf(s->gx * s->gx + s->gy * s->gy + s->gz * s->gz);
    if (gyro_mag > 25.0f || fabsf(m->band) > 0.05f) m->last_motion_ms = now_ms;

    if (m->band < -STEP_REARM) m->armed = true;

    uint32_t since = now_ms - m->last_step_ms;
    if (m->armed && m->band > STEP_THRESHOLD && m->band_prev <= STEP_THRESHOLD &&
        (m->last_step_ms == 0 || since >= STEP_MIN_GAP_MS)) {
        m->steps++;
        m->last_step_ms = now_ms;
        m->armed = false;
    }
    if (m->last_step_ms && since > STEP_MAX_GAP_MS) m->armed = true;
    m->band_prev = m->band;

    /* ---- wrist raise ----------------------------------------------------
     * The glass must end up facing the wearer, the watch must be settling
     * rather than swinging, and there must have been real movement recently.
     * Without the movement test a watch resting face-up on a desk would keep
     * waking itself. */
    bool face_up = uz > RAISE_FACE_UP;
    bool settling = gyro_mag < 45.0f;
    bool recent_motion = m->last_motion_ms && (now_ms - m->last_motion_ms) < RAISE_MOTION_MS;
    bool cooled = (now_ms - m->last_raise_ms) > RAISE_COOLDOWN;
    if (face_up && settling && recent_motion && cooled) {
        m->last_raise_ms = now_ms;
        return true;
    }
    return false;
}
