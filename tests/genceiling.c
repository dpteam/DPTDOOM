// Boom generalized ceiling bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// Same bit scheme as generalized floors, but different target numbering and a
// different meaning for the crush bit. Both are easy to get subtly wrong, so the
// decoder is checked exhaustively against BOOM's documented encoding.
#include "dpt_genceiling.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    int trigger, speed, up, target_bits, change, crush;
    int special;
    dpt_genceiling_t ceiling;

    // Range bounds: ceilings 0x4000..0x5fff, floors start at 0x6000.
    REQUIRE(!DPT_GenCeilingIsCeiling(0x3fff));
    REQUIRE(DPT_GenCeilingIsCeiling(0x4000));
    REQUIRE(DPT_GenCeilingIsCeiling(0x5fff));
    REQUIRE(!DPT_GenCeilingIsCeiling(0x6000));
    REQUIRE(!DPT_GenCeilingDecode(0x6000, &ceiling));
    REQUIRE(!DPT_GenCeilingDecode(0x4000, NULL));

    // 8 * 4 * 2 * 8 * 4 * 2 = 2048 field combinations decode exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (up = 0; up < 2; ++up)
    for (target_bits = 0; target_bits < 8; ++target_bits)
    for (change = 0; change < 4; ++change)
    for (crush = 0; crush < 2; ++crush)
    {
        int raw = target_bits + 1;

        special = 0x4000 | trigger | (speed << 3) | (up << 6)
                | (target_bits << 7) | (change << 10) | (crush << 12);
        REQUIRE(DPT_GenCeilingDecode(special, &ceiling));
        REQUIRE(ceiling.trigger == trigger);
        REQUIRE(ceiling.speed_shift == speed);
        REQUIRE(ceiling.up == (up != 0));
        REQUIRE(ceiling.change == change);
        REQUIRE(ceiling.crush == (crush != 0));

        if (raw >= 7)
        {
            REQUIRE(ceiling.target == 0);
            REQUIRE(ceiling.relative_height == 24 + (raw - 7) * 8);
        }
        else
        {
            REQUIRE(ceiling.target == raw);
            REQUIRE(ceiling.relative_height == 0);
        }
        REQUIRE(ceiling.monsters == false);  // 0x0020 not set here
    }

    // The model bit means "monsters may activate" only without a change.
    REQUIRE(DPT_GenCeilingDecode(0x4000 | 0x0020, &ceiling));
    REQUIRE(ceiling.monsters == true);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | 0x0020 | 0x0400, &ceiling));
    REQUIRE(ceiling.monsters == false);

    // Ceiling target numbering differs from floors: HnC, LnC, NnC, HnF, F, sT.
    REQUIRE(DPT_GenCeilingDecode(0x4000, &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_HNC);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (1 << 7), &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_LNC);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (2 << 7), &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_NNC);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (3 << 7), &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_HNF);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (4 << 7), &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_FLOOR);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (5 << 7), &ceiling));
    REQUIRE(ceiling.target == DPT_CEILING_TARGET_TEXTURE);
    REQUIRE(DPT_GenCeilingDecode(0x4000 | (6 << 7), &ceiling));
    REQUIRE(ceiling.relative_height == 24 && ceiling.target == 0);

    // Supported subset: every target that resolves to a height this engine can
    // compute, in both directions; crush only while lowering. Push triggers act
    // on the sector behind the line. Reported: texture/model change and the
    // "next neighbour ceiling" target, which needs a helper we do not have.
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040));            // up HnC
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7)));          // down F
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7) | 0x1000)); // down F crush
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040 | 0x0002));   // S1 up HnC
    REQUIRE(DPT_GenCeilingSupported(0x4000));                    // down HnC
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (1 << 7)));         // LnC
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (3 << 7)));         // HnF
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (6 << 7)));         // by 24
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040 | (6 << 7))); // up by 24
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040 | (4 << 7))); // up F

    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (2 << 7)));         // NnC
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (5 << 7)));         // sT
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0400));           // texture change
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0040 | 0x1000));  // crush while up
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7) | 0x0006)); // push trigger
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7) | 0x0007)); // push retrigger
    REQUIRE(!DPT_GenCeilingSupported(0x6000));                    // not a ceiling

    // Exhaustive: texture/model change, the next-neighbour target and crush
    // while raising are the only rejections.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (up = 0; up < 2; ++up)
    for (crush = 0; crush < 2; ++crush)
    for (change = 0; change < 4; ++change)
    for (target_bits = 0; target_bits < 8; ++target_bits)
    {
        int expected;
        special = 0x4000 | trigger | (speed << 3) | (up << 6) | (change << 10)
                | (crush << 12) | (target_bits << 7);
        expected = change == 0 && target_bits != 2 && target_bits != 5 && !(crush && up);
        REQUIRE(DPT_GenCeilingSupported(special) == expected);
    }

    // Destination selection, with every neighbour value distinct so an
    // accidental swap between two targets cannot pass.
    {
        dpt_ceiling_env_t env;
        env.current = 0;
        env.highest_ceiling = 200 * FRACUNIT;
        env.lowest_ceiling = 120 * FRACUNIT;
        env.highest_floor = 48 * FRACUNIT;
        env.floor = -16 * FRACUNIT;

        ceiling.target = DPT_CEILING_TARGET_HNC; ceiling.up = 1;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.highest_ceiling);
        ceiling.target = DPT_CEILING_TARGET_LNC; ceiling.up = 0;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.lowest_ceiling);
        ceiling.target = DPT_CEILING_TARGET_HNF; ceiling.up = 1;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.highest_floor);
        ceiling.target = DPT_CEILING_TARGET_FLOOR; ceiling.up = 0;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.floor);

        ceiling.target = 0;
        ceiling.relative_height = 24;
        ceiling.up = 1;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.current + 24*FRACUNIT);
        ceiling.up = 0;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.current - 24*FRACUNIT);
        ceiling.relative_height = 32;
        ceiling.up = 1;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.current + 32*FRACUNIT);

        // No crush offset: BOOM's generalized ceiling stops at its target.
        ceiling.target = DPT_CEILING_TARGET_FLOOR;
        ceiling.up = 0;
        ceiling.crush = 1;
        REQUIRE(DPT_GenCeilingDest(&ceiling, &env) == env.floor);

        REQUIRE(DPT_GenCeilingDest(NULL, &env) == env.current);
        REQUIRE(DPT_GenCeilingDest(&ceiling, NULL) == 0);
    }

    puts("Boom generalized ceilings: range bounds, 2048 field combinations, "
         "ceiling target order, supported targets and destination heights OK");
    return 0;
}
