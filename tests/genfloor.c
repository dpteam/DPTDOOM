// Boom generalized floor bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// The risky part of this feature is the bit layout: a wrong mask or shift moves
// the floor to the wrong height at the wrong speed. This test checks every field
// combination against BOOM's documented encoding, including the target numbering
// and the model/monster bit that changes meaning depending on the change field.
#include "dpt_genfloor.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    int trigger, speed, up, target_bits, change, crush;
    int special;
    dpt_genfloor_t floor;

    // Range bounds: floors 0x6000..0x7fff, ceilings start at 0x4000.
    REQUIRE(!DPT_GenFloorIsFloor(0x5fff));   // generalized ceilings
    REQUIRE(DPT_GenFloorIsFloor(0x6000));
    REQUIRE(DPT_GenFloorIsFloor(0x7fff));
    REQUIRE(!DPT_GenFloorIsFloor(0x8000));
    REQUIRE(!DPT_GenFloorDecode(0x5fff, &floor));
    REQUIRE(!DPT_GenFloorDecode(0x6000, NULL));

    // Every trigger x speed x direction x target x change x crush decodes
    // exactly. 8 * 4 * 2 * 8 * 4 * 2 = 2048 combinations.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (up = 0; up < 2; ++up)
    for (target_bits = 0; target_bits < 8; ++target_bits)
    for (change = 0; change < 4; ++change)
    for (crush = 0; crush < 2; ++crush)
    {
        int raw = target_bits + 1;

        special = 0x6000 | trigger | (speed << 3) | (up << 6)
                | (target_bits << 7) | (change << 10) | (crush << 12);
        REQUIRE(DPT_GenFloorDecode(special, &floor));
        REQUIRE(floor.trigger == trigger);
        REQUIRE(floor.speed_shift == speed);
        REQUIRE(floor.up == (up != 0));
        REQUIRE(floor.change == change);
        REQUIRE(floor.crush == (crush != 0));

        if (raw >= 7)
        {
            // Targets 7..8 become absolute heights 24, 32.
            REQUIRE(floor.target == 0);
            REQUIRE(floor.absolute_height == 24 + (raw - 7) * 8);
        }
        else
        {
            REQUIRE(floor.target == raw);
            REQUIRE(floor.absolute_height == 0);
        }

        // The model bit is also the monster flag, but only without a change.
        REQUIRE(floor.monsters == false); // bit 0x0020 not set here
    }

    // The model/monster bit doubles as "monsters may activate" only when the
    // action does not change textures.
    REQUIRE(DPT_GenFloorDecode(0x6000 | 0x0020, &floor));
    REQUIRE(floor.monsters == true);
    REQUIRE(floor.change == 0);
    REQUIRE(DPT_GenFloorDecode(0x6000 | 0x0020 | 0x0400, &floor));
    REQUIRE(floor.monsters == false);   // change != 0: it selects the model
    REQUIRE(floor.change == 1);

    // Documented target numbering: HnF, LnF, NnF, LnC, C, sT, 24, 32.
    REQUIRE(DPT_GenFloorDecode(0x6000, &floor));        // target 1 HnF, down
    REQUIRE(floor.target == DPT_FLOOR_TARGET_HNF && !floor.up);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (1 << 7), &floor));
    REQUIRE(floor.target == DPT_FLOOR_TARGET_LNF);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (2 << 7), &floor));
    REQUIRE(floor.target == DPT_FLOOR_TARGET_NNF);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (3 << 7), &floor));
    REQUIRE(floor.target == DPT_FLOOR_TARGET_LNC);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (4 << 7), &floor));
    REQUIRE(floor.target == DPT_FLOOR_TARGET_CEIL);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (5 << 7), &floor));
    REQUIRE(floor.target == DPT_FLOOR_TARGET_TEXTURE);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (6 << 7), &floor));
    REQUIRE(floor.absolute_height == 24 && floor.target == 0);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (7 << 7), &floor));
    REQUIRE(floor.absolute_height == 32 && floor.target == 0);

    // Supported subset: exact neighbour mappings only, no texture change, no
    // absolute height, crush only while rising. Push triggers are supported too:
    // they act on the sector behind the line.
    REQUIRE(DPT_GenFloorSupported(0x6000));                  // W1 down HnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | (1 << 7)));       // W1 down LnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (2 << 7))); // up NnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (3 << 7))); // up LnC
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (3 << 7) | 0x1000)); // crush

    REQUIRE(!DPT_GenFloorSupported(0x6000 | (4 << 7)));      // ceiling target
    REQUIRE(!DPT_GenFloorSupported(0x6000 | (5 << 7)));      // shortest texture
    REQUIRE(!DPT_GenFloorSupported(0x6000 | (6 << 7)));      // absolute 24
    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x0400));        // texture change
    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x1000));        // crush while down
    REQUIRE(!DPT_GenFloorSupported(0x6000 | (3 << 7)));      // LnC but down
    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x0040 | (1 << 7))); // LnF but up
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0006));         // push trigger
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0007));         // push retrigger
    REQUIRE(!DPT_GenFloorSupported(0x5fff));                 // not a floor

    puts("Boom generalized floors: range bounds, 2048 field combinations, "
         "model/monster bit and supported-subset rules OK");
    return 0;
}
