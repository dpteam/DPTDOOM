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
            REQUIRE(floor.relative_height == 24 + (raw - 7) * 8);
        }
        else
        {
            REQUIRE(floor.target == raw);
            REQUIRE(floor.relative_height == 0);
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
    REQUIRE(floor.relative_height == 24 && floor.target == 0);
    REQUIRE(DPT_GenFloorDecode(0x6000 | (7 << 7), &floor));
    REQUIRE(floor.relative_height == 32 && floor.target == 0);

    // Supported subset: every target, in both directions, with crush only while
    // rising. Push triggers are supported too: they act on the sector behind the
    // line. Reported: only a texture/model change, which needs model-sector
    // logic, and crush while lowering.
    REQUIRE(DPT_GenFloorSupported(0x6000));                  // W1 down HnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | (1 << 7)));       // W1 down LnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (2 << 7))); // up NnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (3 << 7))); // up LnC
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (3 << 7) | 0x1000)); // crush
    REQUIRE(DPT_GenFloorSupported(0x6000 | (4 << 7)));       // ceiling height
    REQUIRE(DPT_GenFloorSupported(0x6000 | (5 << 7)));       // shortest texture
    REQUIRE(DPT_GenFloorSupported(0x6000 | (6 << 7)));       // by 24
    REQUIRE(DPT_GenFloorSupported(0x6000 | (7 << 7)));       // by 32
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (6 << 7))); // up by 24
    REQUIRE(DPT_GenFloorSupported(0x6000 | (2 << 7)));       // down NnF
    REQUIRE(DPT_GenFloorSupported(0x6000 | (3 << 7)));       // down LnC
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0040 | (5 << 7))); // up by texture

    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x0400));        // texture change
    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x0800));        // texture + type
    REQUIRE(!DPT_GenFloorSupported(0x6000 | 0x1000));        // crush while down
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0006));         // push trigger
    REQUIRE(DPT_GenFloorSupported(0x6000 | 0x0007));         // push retrigger
    REQUIRE(!DPT_GenFloorSupported(0x5fff));                 // not a floor

    // Across every field combination the rule is exactly: a texture/model change
    // and crush while lowering are the only rejections. Enumerating it stops a
    // future edit from silently widening or narrowing support.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (up = 0; up < 2; ++up)
    for (crush = 0; crush < 2; ++crush)
    for (change = 0; change < 4; ++change)
    for (target_bits = 0; target_bits < 8; ++target_bits)
    {
        int expected;
        special = 0x6000 | trigger | (speed << 3) | (up << 6) | (change << 10)
                | (crush << 12) | (target_bits << 7);
        expected = change == 0 && !(crush && !up);
        REQUIRE(DPT_GenFloorSupported(special) == expected);
    }

    // Destination selection. The engine gathers the neighbour heights; the
    // choice of which one to use is where a wrong helper would hide, so every
    // target is checked against a distinct set of values: if two targets share a
    // height, an accidental swap between them would go unnoticed.
    {
        dpt_floor_env_t env;
        env.current = 0;
        env.highest_floor = 64 * FRACUNIT;
        env.lowest_floor = -32 * FRACUNIT;
        env.next_lowest_floor = -8 * FRACUNIT;
        env.next_highest_floor = 16 * FRACUNIT;
        env.lowest_ceiling = 128 * FRACUNIT;
        env.ceiling = 192 * FRACUNIT;

        floor.target = DPT_FLOOR_TARGET_HNF; floor.up = 0;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.highest_floor);
        floor.target = DPT_FLOOR_TARGET_LNF;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.lowest_floor);
        floor.target = DPT_FLOOR_TARGET_LNC;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.lowest_ceiling);
        floor.target = DPT_FLOOR_TARGET_CEIL;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.ceiling);

        // "Next neighbour" follows the direction bit.
        floor.target = DPT_FLOOR_TARGET_NNF;
        floor.up = 0;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.next_lowest_floor);
        floor.up = 1;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.next_highest_floor);

        // Relative 24/32 units, in the direction of travel.
        floor.target = 0;
        floor.relative_height = 24;
        floor.up = 1;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.current + 24*FRACUNIT);
        floor.up = 0;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.current - 24*FRACUNIT);
        floor.relative_height = 32;
        floor.up = 1;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.current + 32*FRACUNIT);

        // The shortest-texture target moves by the texture height, in the
        // direction of travel. This is the one target whose destination depends
        // on the map's textures rather than on neighbour heights.
        env.shortest_lower = 64 * FRACUNIT;
        floor.target = DPT_FLOOR_TARGET_TEXTURE;
        floor.up = 1;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.current + 64*FRACUNIT);
        floor.up = 0;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.current - 64*FRACUNIT);

        // ...and it is clamped in whole units, so a sector near the limit cannot
        // wrap. A 32000-unit texture height on a sector already at 32000 must
        // stay at 32000, not overflow.
        {
            dpt_floor_env_t high = env;
            high.current = 32000 * FRACUNIT;
            high.shortest_lower = 32000 * FRACUNIT;
            floor.target = DPT_FLOOR_TARGET_TEXTURE;
            floor.up = 1;
            REQUIRE(DPT_GenFloorDest(&floor, &high) == 32000 * FRACUNIT);
            high.current = -32000 * FRACUNIT;
            floor.up = 0;
            REQUIRE(DPT_GenFloorDest(&floor, &high) == -32000 * FRACUNIT);
        }

        // A crush offset must not creep in: BOOM moves exactly to the target.
        floor.target = DPT_FLOOR_TARGET_LNC;
        floor.up = 1;
        floor.crush = 1;
        REQUIRE(DPT_GenFloorDest(&floor, &env) == env.lowest_ceiling);

        // Null arguments must not crash or return a bogus height.
        REQUIRE(DPT_GenFloorDest(NULL, &env) == env.current);
        REQUIRE(DPT_GenFloorDest(&floor, NULL) == 0);
    }

    puts("Boom generalized floors: range bounds, 2048 field combinations, "
         "model/monster bit, supported targets and destination heights OK");
    return 0;
}
