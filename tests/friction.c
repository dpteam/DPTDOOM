// Data-free checks for Boom variable friction (linedef 223). No IWAD, no engine.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "dpt_friction.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    dpt_friction_t values;
    int length, previous_friction;

    // A zero-length line is nonsense; clamping it must still produce a usable
    // value instead of dividing the map into an unwalkable state.
    REQUIRE(DPT_FrictionForLength(0, &values));
    REQUIRE(values.friction >= 0 && values.friction <= FRACUNIT);
    REQUIRE(values.movefactor >= 32);

    // Length 0 and length 1 must agree, because 0 is clamped to 1: a difference
    // would mean the clamp is not applied before the formula.
    {
        dpt_friction_t one;
        REQUIRE(DPT_FrictionForLength(1, &one));
        REQUIRE(DPT_FrictionForLength(0, &values));
        REQUIRE(values.friction == one.friction);
        REQUIRE(values.movefactor == one.movefactor);
    }

    // The vanilla default must be reachable, and it is the pivot of the whole
    // feature: below it floors are sticky, above it slippery.
    REQUIRE(DPT_ORIG_FRICTION == 0xE800);
    REQUIRE(DPT_ORIG_FRICTION_FACTOR == 2048);

    // Short lines: friction rises with length up to the vanilla value, i.e. the
    // floor is sticky and becomes less sticky as the line grows.
    previous_friction = -1;
    for (length = 1; length <= 30; ++length)
    {
        REQUIRE(DPT_FrictionForLength(length, &values));
        REQUIRE(values.friction > previous_friction);
        previous_friction = values.friction;
    }

    // Long lines: friction keeps rising past the vanilla value, i.e. the floor
    // becomes slippery. This is the direction that is easy to get backwards.
    {
        dpt_friction_t sticky, vanillaish, icy;
        REQUIRE(DPT_FrictionForLength(10, &sticky));
        REQUIRE(DPT_FrictionForLength(64, &vanillaish));
        REQUIRE(DPT_FrictionForLength(160, &icy));
        REQUIRE(sticky.friction < vanillaish.friction);
        REQUIRE(vanillaish.friction < icy.friction);
        REQUIRE(icy.friction > DPT_ORIG_FRICTION);
    }

    // movefactor runs the opposite way: ice must let the player push off harder,
    // sticky floors must not. If these ever agreed, the formula branches are
    // crossed.
    {
        dpt_friction_t sticky, icy;
        REQUIRE(DPT_FrictionForLength(10, &sticky));
        REQUIRE(DPT_FrictionForLength(160, &icy));
        REQUIRE(icy.movefactor > sticky.movefactor);
        REQUIRE(sticky.movefactor >= 32);
    }

    // The clamp keeps a pathological length from producing friction above
    // FRACUNIT (negative drag, i.e. acceleration) or a tiny movefactor.
    {
        dpt_friction_t huge;
        REQUIRE(DPT_FrictionForLength(100000, &huge));
        REQUIRE(huge.friction <= FRACUNIT);
        REQUIRE(huge.movefactor >= 32);
    }
    // Beyond the documented maximum, results stop changing.
    {
        dpt_friction_t a, b;
        REQUIRE(DPT_FrictionForLength(DPT_FRICTION_MAX_LENGTH, &a));
        REQUIRE(DPT_FrictionForLength(DPT_FRICTION_MAX_LENGTH + 5000, &b));
        REQUIRE(a.friction == b.friction && a.movefactor == b.movefactor);
    }

    // Documented spot values, cross-checked against an independent
    // re-implementation of Boom's arithmetic (Python, same integer divisions).
    // A test that only agrees with itself would not catch a transcription error
    // in the constants.
    {
        static const struct { int length, friction, movefactor; } expected[] = {
            {   1, 53309,   32 },
            {  10, 53862,   32 },
            {  32, 55214,   32 },
            {  64, 57180,   83 },
            {  96, 59146,  236 },
            { 128, 61112, 1487 },
            { 160, 63078,  847 },
            { 200, 65535,   47 }
        };
        unsigned int i;
        for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        {
            REQUIRE(DPT_FrictionForLength(expected[i].length, &values));
            REQUIRE(values.friction == expected[i].friction);
            REQUIRE(values.movefactor == expected[i].movefactor);
        }
        // The vanilla value sits between these two: 96 is still sticky, 128 is
        // already slippery.
        REQUIRE(expected[4].friction < DPT_ORIG_FRICTION);
        REQUIRE(expected[5].friction > DPT_ORIG_FRICTION);
    }

    // Null output is rejected instead of written through.
    REQUIRE(!DPT_FrictionForLength(64, NULL));

    // The flag is what makes a stored friction value take effect at all.
    REQUIRE(DPT_FrictionFlagged(0x0100));
    REQUIRE(DPT_FrictionFlagged(0xE1 | 0x0100));
    REQUIRE(!DPT_FrictionFlagged(0));
    REQUIRE(!DPT_FrictionFlagged(0xE1));
    REQUIRE(!DPT_FrictionFlagged(0x0200));   // push, not friction

    // Overlapping flagged sectors resolve to the slipperier (lower) value.
    REQUIRE(DPT_FrictionSlipperiest(0xE800, 0xF000) == 0xE800);
    REQUIRE(DPT_FrictionSlipperiest(0xF000, 0xE800) == 0xE800);
    REQUIRE(DPT_FrictionSlipperiest(0xD000, 0xD000) == 0xD000);

    puts("Boom variable friction: length formula, direction, clamps, flag and overlap rules OK");
    return 0;
}
