/* stick_deadzone: a stick at rest reads 0 (pads drift off centre), a full push
 * still reaches full range, the direction is kept, and nothing overflows. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "input/stick_deadzone.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    int16_t x, y;

    /* The user's pad at rest: left (-1928, 385), right (899, 385). */
    x = -1928; y = 385; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x == 0 && y == 0, "left rest drift reads 0: %d %d", x, y);
    x = 899; y = 385; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x == 0 && y == 0, "right rest drift reads 0: %d %d", x, y);

    /* Full pushes keep full range. */
    x = 32767; y = 0; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x == 32767 && y == 0, "full right: %d %d", x, y);
    x = -32768; y = 0; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x <= -32767 && y == 0, "full left: %d %d", x, y);
    x = 0; y = -32768; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x == 0 && y <= -32767, "full down: %d %d", x, y);

    /* A corner (radius over 32767) does not overflow and keeps its direction. */
    x = 32767; y = 32767; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x > 23000 && x == y, "corner: %d %d", x, y);
    x = -32768; y = -32768; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x < -23000 && x == y, "negative corner: %d %d", x, y);

    /* Just outside the zone: small but not zero; halfway: rescaled, monotonic. */
    x = STICK_DEADZONE + 400; y = 0; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x > 0 && x < 1500, "just outside: %d", x);
    {
        int16_t a = 16384, b = 0, c = 24000, d = 0;
        stick_deadzone(&a, &b, STICK_DEADZONE);
        stick_deadzone(&c, &d, STICK_DEADZONE);
        CHECK(a > 0 && a < 16384 && c > a, "rescaled and monotonic: %d %d", a, c);
    }

    /* The direction is kept for a diagonal push. */
    x = 20000; y = -10000; stick_deadzone(&x, &y, STICK_DEADZONE);
    CHECK(x > 0 && y < 0 && abs(x - 2 * -y) <= 2, "direction kept: %d %d", x, y);

    /* Range compensation, learned per direction (RECOMP_STICK_RANGE unset). */
    {
        float reach[8] = { 0 };
        int16_t a, b;
        /* A pad that reaches full tilt keeps its analog travel: half a push stays half. */
        a = 32767; b = 0; stick_range_at(&a, &b, reach, 0.0);
        CHECK(a == 32767 && b == 0, "full push stays full: %d %d", a, b);
        a = 16384; b = 0; stick_range_at(&a, &b, reach, 0.0);
        CHECK(a == 16384, "half push after a full one stays half (was stretched to %d)", a);
        /* A pad that falls short (0.67 straight back) is stretched to full there only. */
        a = 0; b = -21900; stick_range_at(&a, &b, reach, 0.0);
        CHECK(a == 0 && b <= -32700, "short direction stretched to full: %d %d", a, b);
        a = 0; b = -10950; stick_range_at(&a, &b, reach, 0.0);
        CHECK(b < -16000 && b > -16700, "half of the short range is half: %d", b);
        /* Before anything is learned the reach is at least 0.60: a small push is not blown up. */
        float fresh[8] = { 0 };
        a = 0; b = 6000; stick_range_at(&a, &b, fresh, 0.0);
        CHECK(b > 9000 && b < 10500, "first small push scaled by 1/0.6 at most: %d", b);
        /* Fixed range (RECOMP_STICK_RANGE=0.65) as before; 1 = off. */
        a = 21299; b = 0; stick_range_at(&a, &b, fresh, 0.65);
        CHECK(a >= 32700, "fixed 0.65: %d", a);
        a = 16384; b = 0; stick_range_at(&a, &b, fresh, 1.0);
        CHECK(a == 16384, "off: %d", a);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("stick_deadzone: all checks passed\n");
    return 0;
}
