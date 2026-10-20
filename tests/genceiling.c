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
            REQUIRE(ceiling.absolute_height == 24 + (raw - 7) * 8);
        }
        else
        {
            REQUIRE(ceiling.target == raw);
            REQUIRE(ceiling.absolute_height == 0);
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
    REQUIRE(ceiling.absolute_height == 24 && ceiling.target == 0);

    // Supported subset: raise to highest neighbour ceiling, or lower to floor
    // with optional crush. Nothing else, and no push triggers.
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040));            // up HnC
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7)));          // down F
    REQUIRE(DPT_GenCeilingSupported(0x4000 | (4 << 7) | 0x1000)); // down F crush
    REQUIRE(DPT_GenCeilingSupported(0x4000 | 0x0040 | 0x0002));   // S1 up HnC

    REQUIRE(!DPT_GenCeilingSupported(0x4000));                    // down HnC
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (1 << 7)));         // LnC
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (2 << 7)));         // NnC
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (3 << 7)));         // HnF
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (5 << 7)));         // sT
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | (6 << 7)));         // absolute
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0400));           // texture change
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0040 | 0x1000));  // crush while up
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0040 | (4 << 7))); // up F
    REQUIRE(!DPT_GenCeilingSupported(0x4000 | 0x0006));           // push trigger
    REQUIRE(!DPT_GenCeilingSupported(0x6000));                    // not a ceiling

    puts("Boom generalized ceilings: range bounds, 2048 field combinations, "
         "ceiling target order and supported-subset rules OK");
    return 0;
}
