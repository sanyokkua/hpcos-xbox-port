/* A radial dead zone for an analog stick (hp1-xbox-port).
 *
 * Modern pads rest a little off centre (a play-tester's: left -1928/385, right
 * 899/385), and nothing between SDL and the title ignored it, so the title saw a
 * constant small push on both sticks. Inside the zone the stick reads 0; outside
 * it the distance is rescaled so the edge of the zone is 0 and a full push is
 * still full range, with the direction kept (a radial zone, so diagonals are not
 * cut the way per-axis zones cut them). */
#ifndef STICK_DEADZONE_H
#define STICK_DEADZONE_H

#include <math.h>
#include <stdint.h>

#define STICK_DEADZONE 3600             /* ~11% of full deflection */

static inline void stick_deadzone(int16_t *x, int16_t *y, int zone)
{
    double fx = *x, fy = *y, r = sqrt(fx * fx + fy * fy), s, nx, ny;

    if (r <= zone) {
        *x = *y = 0;
        return;
    }
    if (r > 32767.0)                    /* corners reach ~46000: onto the circle */
        r = 32767.0;
    s = (r - zone) / (32767.0 - zone) * 32767.0 / sqrt(fx * fx + fy * fy);
    nx = fx * s;
    ny = fy * s;
    *x = (int16_t)(nx > 32767.0 ? 32767 : nx < -32767.0 ? -32767 : lrint(nx));
    *y = (int16_t)(ny > 32767.0 ? 32767 : ny < -32767.0 ? -32767 : lrint(ny));
}

/* Range compensation (after HP-COS-Xbox-Recomp, MIT): deflection at RECOMP_STICK_RANGE
 * of full (default 0.65; 1 = off) reads as full, scaled radially so the direction stays. */
#include <stdlib.h>
static inline void stick_range(int16_t *x, int16_t *y)
{
    static double range = -1.0;
    double fx = *x / 32767.0, fy = *y / 32767.0, m, s;
    if (range < 0.0) {
        const char *e = getenv("RECOMP_STICK_RANGE");
        range = e && *e ? atof(e) : 0.65;
        if (range <= 0.05 || range > 1.0) range = 1.0;
    }
    if (range >= 1.0) return;
    m = sqrt(fx * fx + fy * fy);
    if (m <= 0.0) return;
    s = 1.0 / range;
    if (m * s > 1.0) s = 1.0 / m;       /* onto the unit circle */
    fx *= s; fy *= s;
    *x = (int16_t)lrint((fx > 1.0 ? 1.0 : fx < -1.0 ? -1.0 : fx) * 32767.0);
    *y = (int16_t)lrint((fy > 1.0 ? 1.0 : fy < -1.0 ? -1.0 : fy) * 32767.0);
}

#endif
