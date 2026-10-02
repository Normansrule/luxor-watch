/*
 * Host-side motion tests.
 *   cc -std=c11 -I../main -o test_motion test_motion.c ../main/motion.c -lm
 */
#include "motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static imu_sample_t flat(void)
{
    imu_sample_t s = {0};
    s.az = 1.0f;                       /* glass up, at rest */
    return s;
}

int main(void)
{
    motion_t m;
    imu_sample_t s;

    /* ---- 60 s perfectly still: no phantom steps, no phantom wake ------- */
    motion_init(&m, 0);
    s = flat();
    for (unsigned i = 1; i <= 3000; i++) assert(!motion_update(&m, &s, 0.02f, i * 20, NULL));
    assert(m.steps == 0);
    assert(fabsf(m.tilt_x) < 0.001f && fabsf(m.tilt_y) < 0.001f);
    puts("PASS: 60 s stationary - zero false steps, zero false wakes");

    /* ---- tilt directions map to screen directions ---------------------- */
    motion_init(&m, 0);
    s = flat();
    s.ax = -0.5f; s.az = 0.866f;        /* 3 o'clock edge pushed down */
    for (unsigned i = 1; i <= 400; i++) motion_update(&m, &s, 0.02f, i * 20, NULL);
    assert(m.tilt_x > 0.4f);
    assert(fabsf(m.tilt_y) < 0.05f);
    printf("   3 o'clock edge down -> tilt_x = %+.2f (ball rolls right)\n", m.tilt_x);

    motion_init(&m, 0);
    s = flat();
    s.ay = 0.5f; s.az = 0.866f;         /* 6 o'clock / USB-C edge pushed down */
    for (unsigned i = 1; i <= 400; i++) motion_update(&m, &s, 0.02f, i * 20, NULL);
    assert(m.tilt_y > 0.4f);
    assert(fabsf(m.tilt_x) < 0.05f);
    printf("   6 o'clock edge down -> tilt_y = %+.2f (ball rolls down)\n", m.tilt_y);

    motion_init(&m, 0);
    s = flat();
    s.ax = 0.5f; s.az = 0.866f;         /* 9 o'clock edge down */
    for (unsigned i = 1; i <= 400; i++) motion_update(&m, &s, 0.02f, i * 20, NULL);
    assert(m.tilt_x < -0.4f);
    puts("PASS: tilt is reported in screen directions, not sensor directions");

    /* ---- tilt must not drift while the watch is held at an angle ------- */
    motion_init(&m, 0);
    s = flat();
    s.ax = -0.34f; s.az = 0.94f;
    for (unsigned i = 1; i <= 200; i++) motion_update(&m, &s, 0.02f, i * 20, NULL);
    float settled = m.tilt_x;
    s.gz = 40.0f;                        /* a steady gyro offset, as a biased part gives */
    for (unsigned i = 201; i <= 3200; i++) motion_update(&m, &s, 0.02f, i * 20, NULL);
    printf("   held at %+.3f, after 60 s with a 40 deg/s gyro offset: %+.3f\n", settled, m.tilt_x);
    assert(fabsf(m.tilt_x - settled) < 0.01f);
    puts("PASS: tilt comes from gravity only - a biased gyroscope cannot walk it");

    /* ---- synthetic 2 Hz walking signal --------------------------------- */
    motion_init(&m, 0);
    s = flat();
    for (unsigned i = 1; i <= 3000; i++) {
        s.az = 1.0f + 0.28f * sinf(6.28318530718f * 2.0f * i * 0.02f);
        motion_update(&m, &s, 0.02f, i * 20, NULL);
    }
    printf("   synthetic 2 Hz gait: %u steps in 60 s\n", m.steps);
    assert(m.steps >= 112 && m.steps <= 122);
    puts("PASS: synthetic step count is in range (not human validation)");

    /* ---- a slow tilt must not be counted as walking -------------------- */
    motion_init(&m, 0);
    for (unsigned i = 1; i <= 2000; i++) {
        float t = i * 0.02f;
        float ang = 0.9f * sinf(6.28318530718f * 0.15f * t);   /* 0.15 Hz sway */
        s = flat();
        s.ax = -sinf(ang); s.az = cosf(ang); s.gx = 0; s.gy = 0; s.gz = 0;
        motion_update(&m, &s, 0.02f, i * 20, NULL);
    }
    printf("   40 s of slow arm sway: %u steps\n", m.steps);
    assert(m.steps <= 2);
    puts("PASS: slow posture changes do not ratchet the step counter");

    /* ---- wrist raise: needs real movement, then fires once ------------- */
    motion_init(&m, 0);
    s = flat(); s.az = 0.2f; s.ay = -0.97f; s.gx = 120.0f;   /* arm hanging, moving */
    for (unsigned i = 1; i <= 60; i++) motion_update(&m, &s, 0.02f, 5000 + i * 20, NULL);
    s = flat(); s.gx = 10.0f;                                 /* brought up to the face */
    bool fired = false;
    for (unsigned i = 1; i <= 120; i++) if (motion_update(&m, &s, 0.02f, 6300 + i * 20, NULL)) fired = true;
    assert(fired);
    bool again = false;
    for (unsigned i = 1; i <= 60; i++) if (motion_update(&m, &s, 0.02f, 6600 + i * 20, NULL)) again = true;
    assert(!again);
    puts("PASS: wrist raise fires once and then respects the cooldown");

    /* ---- garbage in, nothing out --------------------------------------- */
    motion_init(&m, 0);
    uint32_t before = m.steps;
    s = flat(); s.ax = NAN;
    assert(!motion_update(&m, &s, 0.02f, 100, NULL));
    assert(m.steps == before);
    s = flat();
    assert(!motion_update(&m, &s, 1.0f, 200, NULL));          /* absurd dt */
    assert(!motion_update(&m, &s, -0.01f, 300, NULL));
    assert(!motion_update(&m, NULL, 0.02f, 400, NULL));
    puts("PASS: invalid samples and timing are rejected");

    /* ---- stillness detector used by gyro zeroing ----------------------- */
    s = flat();
    assert(motion_is_still(&s));
    s.gx = 90.0f;
    assert(!motion_is_still(&s));
    puts("PASS: stillness detector");

    puts("\nALL MOTION CHECKS PASSED");
    return 0;
}
