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

/* Range compensation (after HP-COS-Xbox-Recomp and HP-POA-Xbox-Recomp, MIT; facts only): some pads fall short of
 * full tilt in a direction (one read -21900 held straight back), and the character walked where it should run.
 * Learned (the default, RECOMP_STICK_RANGE unset): each of 8 directions keeps the furthest the stick has reached
 * there (at least 0.60, at most 1) and a reading is scaled by its direction's reach, radially, so a pad that reaches
 * full tilt keeps its whole travel (the titles' slow walk, walk and run speeds) and a short one is stretched to full
 * where it falls short. A fixed fraction (RECOMP_STICK_RANGE=0.65: a push to 65 % reads as full) was the default
 * before: on a pad that reaches full tilt it made 92 % of a play test's stick readings full, and the walking range
 * a third of the travel (hppoa-xbox-port, PLAYTEST1 #5). RECOMP_STICK_RANGE=1 is off. */
#include <stdlib.h>
static inline void stick_range_at(int16_t *x, int16_t *y, float reach[8], double fixed)
{
    double fx = *x / 32767.0, fy = *y / 32767.0, m, r, s;
    if (fixed >= 1.0) return;
    m = sqrt(fx * fx + fy * fy);
    if (m <= 0.0) return;
    if (fixed > 0.0) {
        r = fixed;
    } else {
        int sec = ((int)lrint(atan2(fy, fx) / 0.7853981634) + 8) & 7;
        if (reach[sec] < 0.60f) reach[sec] = 0.60f;
        if (m > reach[sec]) reach[sec] = m > 1.0 ? 1.0f : (float)m;
        r = reach[sec];
    }
    s = 1.0 / r;
    if (m * s > 1.0) s = 1.0 / m;       /* onto the unit circle */
    fx *= s; fy *= s;
    *x = (int16_t)lrint((fx > 1.0 ? 1.0 : fx < -1.0 ? -1.0 : fx) * 32767.0);
    *y = (int16_t)lrint((fy > 1.0 ? 1.0 : fy < -1.0 ? -1.0 : fy) * 32767.0);
}

/* The setting: 0 = learned, a fraction = fixed, 1 = off. */
static inline double stick_range_setting(void)
{
    static double range = -1.0;
    if (range < 0.0) {
        const char *e = getenv("RECOMP_STICK_RANGE");
        range = e && *e ? atof(e) : 0.0;
        if (range > 1.0 || (range > 0.0 && range <= 0.05)) range = 1.0;
        if (range < 0.0) range = 0.0;
    }
    return range;
}

/* The left stick of pad `port` (0..3). */
static inline void stick_range(int16_t *x, int16_t *y, int port)
{
    static float reach[4][8];
    stick_range_at(x, y, reach[port & 3], stick_range_setting());
}

#endif
