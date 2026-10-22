// Data-free checks for Boom pusher sectors (wind and current). No IWAD, no engine.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "dpt_push.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    dpt_push_t force;

    // Magnitudes: Boom shifts the fixed-point delta down to whole map units, and
    // a negative direction must stay negative rather than becoming huge.
    REQUIRE(DPT_PushMagnitude(64 << 16) == 64);
    REQUIRE(DPT_PushMagnitude(-64 << 16) == -64);
    REQUIRE(DPT_PushMagnitude(0) == 0);

    // Only 224 and 225 create pushers. 226 is the point-source variant and must
    // NOT be read as wind - that would silently give a map the wrong mechanic.
    REQUIRE(DPT_PushTypeForLine(224) == DPT_PUSH_WIND);
    REQUIRE(DPT_PushTypeForLine(225) == DPT_PUSH_CURRENT);
    REQUIRE(DPT_PushTypeForLine(226) == DPT_PUSH_NONE);
    REQUIRE(DPT_PushTypeForLine(223) == DPT_PUSH_NONE);
    REQUIRE(DPT_PushTypeForLine(0) == DPT_PUSH_NONE);

    // Wind: full force above ground, half on the ground. This direction is the
    // whole point of the feature and is easy to invert.
    force = DPT_PushForce(DPT_PUSH_WIND, 1, 64, -32);
    REQUIRE(force.x == 64 && force.y == -32);
    force = DPT_PushForce(DPT_PUSH_WIND, 0, 64, -32);
    REQUIRE(force.x == 32 && force.y == -16);

    // Current: nothing above ground, full force on the ground - the opposite
    // height rule to wind, which is the trap.
    force = DPT_PushForce(DPT_PUSH_CURRENT, 1, 64, -32);
    REQUIRE(force.x == 0 && force.y == 0);
    force = DPT_PushForce(DPT_PUSH_CURRENT, 0, 64, -32);
    REQUIRE(force.x == 64 && force.y == -32);

    // The two types must never agree at either height, or a mixed-up type would
    // be invisible.
    REQUIRE(DPT_PushForce(DPT_PUSH_WIND, 1, 10, 0).x
            != DPT_PushForce(DPT_PUSH_CURRENT, 1, 10, 0).x);
    REQUIRE(DPT_PushForce(DPT_PUSH_WIND, 0, 10, 0).x
            != DPT_PushForce(DPT_PUSH_CURRENT, 0, 10, 0).x);

    // Halving must floor toward negative infinity like Boom's arithmetic shift,
    // not toward zero: -32 halves to -16, but -33 must halve to -17.
    force = DPT_PushForce(DPT_PUSH_WIND, 0, -33, 0);
    REQUIRE(force.x == -17);

    // No pusher type yields no force at either height.
    force = DPT_PushForce(DPT_PUSH_NONE, 0, 64, 64);
    REQUIRE(force.x == 0 && force.y == 0);
    force = DPT_PushForce(DPT_PUSH_NONE, 1, 64, 64);
    REQUIRE(force.x == 0 && force.y == 0);

    // A zero-length line produces zero force rather than a random direction.
    force = DPT_PushForce(DPT_PUSH_WIND, 1, 0, 0);
    REQUIRE(force.x == 0 && force.y == 0);

    // The flag is what makes a pusher run at all.
    REQUIRE(DPT_PushFlagged(0x0200));
    REQUIRE(DPT_PushFlagged(0xE1 | 0x0200));
    REQUIRE(!DPT_PushFlagged(0));
    REQUIRE(!DPT_PushFlagged(0xE1));
    REQUIRE(!DPT_PushFlagged(0x0100));   // friction, not push

    puts("Boom pushers: magnitudes, type mapping, wind/current height rules, flag OK");
    return 0;
}
