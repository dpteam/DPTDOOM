// Boom generalized lift bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// Lifts have their own speed table (P_SLOW..P_TURBO = 8/16/32/64 eighths of a
// unit per tic, a clean doubling) and a 1-based target field. Only the lowest
// neighbour floor target maps onto this engine's downWaitUpStay platform, so the
// supported-subset rules matter as much as the decoding.
#include "dpt_genlift.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    static const int delays[4] = {8, 24, 40, 80};
    static const int speeds[4] = {8, 16, 32, 64};
    int trigger, speed, monster, delay, target, special;
    dpt_genlift_t lift;

    // Range bounds: lifts 0x3400..0x37ff, locked doors start at 0x3800.
    REQUIRE(!DPT_GenLiftIsLift(0x33ff));   // stairs
    REQUIRE(DPT_GenLiftIsLift(0x3400));
    REQUIRE(DPT_GenLiftIsLift(0x37ff));
    REQUIRE(!DPT_GenLiftIsLift(0x3800));
    REQUIRE(!DPT_GenLiftDecode(0x3800, &lift));
    REQUIRE(!DPT_GenLiftDecode(0x3400, NULL));

    // All 8 * 4 * 2 * 4 * 4 = 1024 field combinations decode exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (monster = 0; monster < 2; ++monster)
    for (delay = 0; delay < 4; ++delay)
    for (target = 0; target < 4; ++target)
    {
        special = 0x3400 | trigger | (speed << 3) | (monster << 5)
                | (delay << 6) | (target << 8);
        REQUIRE(DPT_GenLiftDecode(special, &lift));
        REQUIRE(lift.trigger == trigger);
        REQUIRE(lift.speed_eighths == speeds[speed]);
        REQUIRE(lift.monsters == (monster != 0));
        REQUIRE(lift.delay_tics == delays[delay]);
        REQUIRE(lift.target == target + 1);
    }

    // Documented spot values: W1, slow, 8 tics, lowest neighbour floor.
    REQUIRE(DPT_GenLiftDecode(0x3400, &lift));
    REQUIRE(lift.trigger == 0 && lift.speed_eighths == 8);
    REQUIRE(lift.delay_tics == 8 && lift.target == DPT_LIFT_TARGET_LNF);
    // SR, turbo, 80 tics, target 4 (perpetual).
    REQUIRE(DPT_GenLiftDecode(0x3400 | 3 | (3 << 3) | (3 << 6) | (3 << 8), &lift));
    REQUIRE(lift.trigger == 3 && lift.speed_eighths == 64);
    REQUIRE(lift.delay_tics == 80 && lift.target == DPT_LIFT_TARGET_PERPETUAL);

    // Supported: only target 1 (lowest neighbour floor), but every trigger class
    // including push.
    REQUIRE(DPT_GenLiftSupported(0x3400));                    // W1 LNF
    REQUIRE(DPT_GenLiftSupported(0x3401));                    // WR
    REQUIRE(DPT_GenLiftSupported(0x3402));                    // S1
    REQUIRE(DPT_GenLiftSupported(0x3403));                    // SR
    REQUIRE(DPT_GenLiftSupported(0x3404));                    // G1
    REQUIRE(DPT_GenLiftSupported(0x3405));                    // GR
    REQUIRE(DPT_GenLiftSupported(0x3400 | 0x00c0));           // 80 tic delay
    REQUIRE(DPT_GenLiftSupported(0x3400 | 0x0018));           // turbo

    REQUIRE(!DPT_GenLiftSupported(0x3400 | (1 << 8)));        // next lowest floor
    REQUIRE(!DPT_GenLiftSupported(0x3400 | (2 << 8)));        // lowest ceiling
    REQUIRE(!DPT_GenLiftSupported(0x3400 | (3 << 8)));        // perpetual
    REQUIRE(DPT_GenLiftSupported(0x3406));                    // P1
    REQUIRE(DPT_GenLiftSupported(0x3407));                    // PR
    REQUIRE(!DPT_GenLiftSupported(0x3000));                   // not a lift

    puts("Boom generalized lifts: range bounds, 1024 field combinations, "
         "speed/delay tables and supported-target rules OK");
    return 0;
}
