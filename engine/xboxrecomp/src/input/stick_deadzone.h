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

#endif
