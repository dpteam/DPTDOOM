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
    static const int delays[4] = {35, 105, 175, 350};
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

    // Documented spot values: W1, slow, 1 second (35 tics), lowest neighbour floor.
    REQUIRE(DPT_GenLiftDecode(0x3400, &lift));
    REQUIRE(lift.trigger == 0 && lift.speed_eighths == 8);
    REQUIRE(lift.delay_tics == 35 && lift.target == DPT_LIFT_TARGET_LNF);
    // SR, turbo, 10 seconds (350 tics), target 4 (perpetual).
    REQUIRE(DPT_GenLiftDecode(0x3400 | 3 | (3 << 3) | (3 << 6) | (3 << 8), &lift));
    REQUIRE(lift.trigger == 3 && lift.speed_eighths == 64);
    REQUIRE(lift.delay_tics == 350 && lift.target == DPT_LIFT_TARGET_PERPETUAL);

    // Supported: targets 1..3 (lowest / next lowest neighbour floor, lowest
    // neighbour ceiling), every trigger class including push. Only the
    // perpetual target is reported.
    REQUIRE(DPT_GenLiftSupported(0x3400));                    // W1 LNF
    REQUIRE(DPT_GenLiftSupported(0x3401));                    // WR
    REQUIRE(DPT_GenLiftSupported(0x3402));                    // S1
    REQUIRE(DPT_GenLiftSupported(0x3403));                    // SR
    REQUIRE(DPT_GenLiftSupported(0x3404));                    // G1
    REQUIRE(DPT_GenLiftSupported(0x3405));                    // GR
    REQUIRE(DPT_GenLiftSupported(0x3400 | 0x00c0));           // 10 s delay
    REQUIRE(DPT_GenLiftSupported(0x3400 | 0x0018));           // turbo

    REQUIRE(DPT_GenLiftSupported(0x3400 | (1 << 8)));         // next lowest floor
    REQUIRE(DPT_GenLiftSupported(0x3400 | (2 << 8)));         // lowest ceiling
    REQUIRE(DPT_GenLiftSupported(0x3400 | (3 << 8)));         // perpetual
    REQUIRE(DPT_GenLiftSupported(0x3406));                    // P1
    REQUIRE(DPT_GenLiftSupported(0x3407));                    // PR
    REQUIRE(!DPT_GenLiftSupported(0x3000));                   // not a lift

    // All four targets are supported, so no combination is rejected.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (delay = 0; delay < 4; ++delay)
    for (target = 0; target < 4; ++target)
    {
        special = 0x3400 | trigger | (speed << 3) | (delay << 6) | (target << 8);
        REQUIRE(DPT_GenLiftSupported(special));
    }

    // Destination (the low point) for each supported target, with distinct
    // neighbour values so a swap between targets cannot pass.
    {
        dpt_lift_env_t env;
        env.current = 0;
        env.lowest_floor = -64 * FRACUNIT;
        env.next_lowest_floor = -16 * FRACUNIT;
        env.lowest_ceiling = -8 * FRACUNIT;

        lift.target = DPT_LIFT_TARGET_LNF;
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.lowest_floor);
        lift.target = DPT_LIFT_TARGET_NNLF;
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.next_lowest_floor);
        lift.target = DPT_LIFT_TARGET_LNC;
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.lowest_ceiling);

        // The ceiling target is clamped to the current floor: a platform must
        // never start above its own high point.
        env.lowest_ceiling = 40 * FRACUNIT;
        lift.target = DPT_LIFT_TARGET_LNC;
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.current);
        // ...while the next-lowest target is not clamped, matching BOOM: with
        // nothing lower the platform simply does not travel.
        env.next_lowest_floor = env.current;
        lift.target = DPT_LIFT_TARGET_NNLF;
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.current);

        REQUIRE(DPT_GenLiftDest(NULL, &env) == env.current);
        REQUIRE(DPT_GenLiftDest(&lift, NULL) == 0);

        // High point: every target except perpetual stops where it started, so
        // the platform returns to its own floor. Perpetual rises to the highest
        // neighbour floor, clamped so it cannot start below itself.
        env.highest_floor = 96 * FRACUNIT;
        lift.target = DPT_LIFT_TARGET_LNF;
        REQUIRE(DPT_GenLiftHigh(&lift, &env) == env.current);
        lift.target = DPT_LIFT_TARGET_PERPETUAL;
        REQUIRE(DPT_GenLiftHigh(&lift, &env) == env.highest_floor);
        REQUIRE(DPT_GenLiftDest(&lift, &env) == env.lowest_floor);

        // Highest neighbour below the current floor: clamp to the current floor
        // rather than producing a platform that only travels downwards.
        env.highest_floor = -40 * FRACUNIT;
        REQUIRE(DPT_GenLiftHigh(&lift, &env) == env.current);

        REQUIRE(DPT_GenLiftHigh(NULL, &env) == env.current);
        REQUIRE(DPT_GenLiftHigh(&lift, NULL) == 0);
    }

    puts("Boom generalized lifts: range bounds, 1024 field combinations, "
         "speed/delay tables, supported targets and destinations OK");
    return 0;
}
