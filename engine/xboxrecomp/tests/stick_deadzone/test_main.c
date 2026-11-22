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

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("stick_deadzone: all checks passed\n");
    return 0;
}
