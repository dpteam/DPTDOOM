// Boom generalized stair bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// The risky part here is the speed table: stair speeds are NOT powers of two.
// csDoomOld07's p_xlat.cpp documents them as eighths of a unit per tic
// (2/4/16/32), which maps onto the vanilla build8 and turbo16 speeds. Deriving
// them with a shift would silently produce 2/4/8/16 instead.
#include "dpt_genstairs.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    static const int steps[4] = {4, 8, 16, 24};
    static const int speeds[4] = {2, 4, 16, 32};
    int trigger, speed, monster, step, dir, igntxt, special;
    dpt_genstairs_t stairs;

    // Range bounds: stairs 0x3000..0x33ff, lifts start at 0x3400.
    REQUIRE(!DPT_GenStairsIsStairs(0x2fff));  // crushers
    REQUIRE(DPT_GenStairsIsStairs(0x3000));
    REQUIRE(DPT_GenStairsIsStairs(0x33ff));
    REQUIRE(!DPT_GenStairsIsStairs(0x3400));
    REQUIRE(!DPT_GenStairsDecode(0x3400, &stairs));
    REQUIRE(!DPT_GenStairsDecode(0x3000, NULL));

    // All 8 * 4 * 2 * 4 * 2 * 2 = 512 field combinations decode exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (monster = 0; monster < 2; ++monster)
    for (step = 0; step < 4; ++step)
    for (dir = 0; dir < 2; ++dir)
    for (igntxt = 0; igntxt < 2; ++igntxt)
    {
        special = 0x3000 | trigger | (speed << 3) | (monster << 5)
                | (step << 6) | (dir << 8) | (igntxt << 9);
        REQUIRE(DPT_GenStairsDecode(special, &stairs));
        REQUIRE(stairs.trigger == trigger);
        REQUIRE(stairs.speed_eighths == speeds[speed]);
        REQUIRE(stairs.monsters == (monster != 0));
        REQUIRE(stairs.step == steps[step]);
        REQUIRE(stairs.up == (dir != 0));
        REQUIRE(stairs.ignore_texture == (igntxt != 0));
    }

    // The speed table is the documented one, not a power-of-two shift.
    REQUIRE(DPT_GenStairsDecode(0x3000 | (0 << 3), &stairs));
    REQUIRE(stairs.speed_eighths == 2);   // S_SLOW  -> FLOORSPEED/4
    REQUIRE(DPT_GenStairsDecode(0x3000 | (1 << 3), &stairs));
    REQUIRE(stairs.speed_eighths == 4);   // S_NORMAL
    REQUIRE(DPT_GenStairsDecode(0x3000 | (2 << 3), &stairs));
    REQUIRE(stairs.speed_eighths == 16);  // S_FAST  (not 8)
    REQUIRE(DPT_GenStairsDecode(0x3000 | (3 << 3), &stairs));
    REQUIRE(stairs.speed_eighths == 32);  // S_TURBO -> FLOORSPEED*4

    // Documented spot values: W1 slow step 8 up = the vanilla build8 shape.
    REQUIRE(DPT_GenStairsDecode(0x3000 | (1 << 6) | (1 << 8), &stairs));
    REQUIRE(stairs.trigger == 0 && stairs.speed_eighths == 2);
    REQUIRE(stairs.step == 8 && stairs.up);
    // SR turbo step 16 up = the vanilla turbo16 shape.
    REQUIRE(DPT_GenStairsDecode(0x3000 | 3 | (3 << 3) | (2 << 6) | (1 << 8), &stairs));
    REQUIRE(stairs.trigger == 3 && stairs.speed_eighths == 32 && stairs.step == 16);

    // Trigger support: every class except push.
    REQUIRE(DPT_GenStairsSupported(0x3000));       // W1
    REQUIRE(DPT_GenStairsSupported(0x3001));       // WR
    REQUIRE(DPT_GenStairsSupported(0x3002));       // S1
    REQUIRE(DPT_GenStairsSupported(0x3003));       // SR
    REQUIRE(DPT_GenStairsSupported(0x3004));       // G1
    REQUIRE(DPT_GenStairsSupported(0x3005));       // GR
    REQUIRE(!DPT_GenStairsSupported(0x3006));      // P1: no back-sector path
    REQUIRE(!DPT_GenStairsSupported(0x3007));      // PR
    REQUIRE(!DPT_GenStairsSupported(0x3400));      // not stairs

    puts("Boom generalized stairs: range bounds, 512 field combinations, "
         "documented speed table and trigger support rules OK");
    return 0;
}
