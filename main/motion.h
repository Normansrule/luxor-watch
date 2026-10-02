#pragma once
/*
 * Luxor motion estimation.
 *
 * Everything here works in the watch frame described in luxconf.h:
 *   +X = 3 o'clock, +Y = 12 o'clock, +Z = out of the glass.
 *
 * The important consequence is that tilt is reported in *screen* directions,
 * so the tilt maze, the bubble level and any future gesture all agree without
 * anyone having to re-derive a sign:
 *
 *   tilt_x > 0  =>  the 3 o'clock edge is lower  =>  a ball rolls right
 *   tilt_y > 0  =>  the 6 o'clock edge is lower  =>  a ball rolls down
 *
 * (tilt_y flips the accelerometer's Y because screen y grows downwards while
 *  the watch frame's Y grows upwards.  This is the only place that conversion
 *  happens.)
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float ax, ay, az;     /* accelerometer, g,     watch frame */
    float gx, gy, gz;     /* gyroscope,     deg/s, watch frame */
} imu_sample_t;

typedef struct {
    /* Smoothed gravity direction, watch frame, roughly unit length. */
    float gx_f, gy_f, gz_f;

    /* Screen-direction tilt, -1..+1.  See the note above. */
    float tilt_x, tilt_y;

    /* Convenience angles in degrees, for display only. */
    float roll_deg;       /* + = 3 o'clock edge down */
    float pitch_deg;      /* + = 6 o'clock edge down */
    float yaw_deg;        /* integrated, relative, drifts */

    /* Step estimation state. */
    float gravity_mag, band, band_prev;
    uint32_t steps, last_step_ms, last_motion_ms, last_raise_ms;
    bool armed, initialized;
} motion_t;

void motion_init(motion_t *m, uint32_t steps);

/*
 * Feed one IMU sample.  gyro_bias may be NULL.  Returns true exactly once per
 * wrist-raise gesture (glass turned towards the face after real movement).
 */
bool motion_update(motion_t *m, const imu_sample_t *s, float dt,
                   uint32_t now_ms, const float gyro_bias[3]);

/* True while the watch is essentially still - used by gyroscope zeroing to
 * decide whether a sample is worth averaging. */
bool motion_is_still(const imu_sample_t *s);
