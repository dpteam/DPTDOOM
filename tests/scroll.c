// Data-free checks for Boom scrollers. No IWAD, no engine.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "dpt_scroll.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    dpt_scroll_t offsets;

    // Type mapping. 48 and 85 are the two vanilla-era wall scrollers; 250-254 are
    // Boom's. Anything else must not be treated as a scroller.
    REQUIRE(DPT_ScrollKindForLine(250) == DPT_SCROLL_CEILING);
    REQUIRE(DPT_ScrollKindForLine(251) == DPT_SCROLL_FLOOR);
    REQUIRE(DPT_ScrollKindForLine(252) == DPT_SCROLL_CARRY);
    REQUIRE(DPT_ScrollKindForLine(253) == DPT_SCROLL_FLOOR_CARRY);
    REQUIRE(DPT_ScrollKindForLine(254) == DPT_SCROLL_WALL);
    REQUIRE(DPT_ScrollKindForLine(48) == DPT_SCROLL_WALL_FIRST);
    REQUIRE(DPT_ScrollKindForLine(85) == DPT_SCROLL_WALL_FIRST);
    REQUIRE(DPT_ScrollKindForLine(0) == DPT_SCROLL_NONE);
    REQUIRE(DPT_ScrollKindForLine(223) == DPT_SCROLL_NONE);
    REQUIRE(DPT_ScrollKindForLine(249) == DPT_SCROLL_NONE);
    REQUIRE(DPT_ScrollKindForLine(255) == DPT_SCROLL_NONE);

    // The two stateful families are scrollers we cannot do without a savegame
    // field, and must be distinguishable from "not a scroller at all".
    REQUIRE(DPT_ScrollNeedsState(214));
    REQUIRE(DPT_ScrollNeedsState(218));
    REQUIRE(DPT_ScrollNeedsState(245));
    REQUIRE(DPT_ScrollNeedsState(249));
    REQUIRE(!DPT_ScrollNeedsState(213));
    REQUIRE(!DPT_ScrollNeedsState(219));
    REQUIRE(!DPT_ScrollNeedsState(250));
    REQUIRE(!DPT_ScrollNeedsState(244));

    // Speed: the linedef delta shifted right by SCROLL_SHIFT, sign preserved, so
    // a line drawn the other way scrolls the other way.
    REQUIRE(DPT_SCROLL_SHIFT == 5);
    REQUIRE(DPT_ScrollSpeed(64 << 16) == (64 << 16) >> 5);
    REQUIRE(DPT_ScrollSpeed(-64 << 16) == (-64 << 16) >> 5);
    REQUIRE(DPT_ScrollSpeed(0) == 0);
    REQUIRE(DPT_ScrollSpeed(-64 << 16) < 0);

    // Carry: 3/32 of the flat speed, same sign.
    REQUIRE(DPT_SCROLL_CARRY_FACTOR == 6144);
    REQUIRE(DPT_ScrollCarrySpeed(0) == 0);
    REQUIRE(DPT_ScrollCarrySpeed(FRACUNIT) == DPT_SCROLL_CARRY_FACTOR);
    REQUIRE(DPT_ScrollCarrySpeed(-FRACUNIT) == -DPT_SCROLL_CARRY_FACTOR);
    // ...and strictly slower than the flat itself, or objects would outrun it.
    REQUIRE(DPT_ScrollCarrySpeed(FRACUNIT) < FRACUNIT);

    // Flat sign rules: floor and ceiling negate x and keep y; carrying negates
    // neither. Getting this uniform would scroll one axis backwards.
    offsets = DPT_ScrollFlatOffsets(DPT_SCROLL_FLOOR, 100, 200);
    REQUIRE(offsets.x == -100 && offsets.y == 200);
    offsets = DPT_ScrollFlatOffsets(DPT_SCROLL_CEILING, 100, 200);
    REQUIRE(offsets.x == -100 && offsets.y == 200);
    offsets = DPT_ScrollFlatOffsets(DPT_SCROLL_FLOOR_CARRY, 100, 200);
    REQUIRE(offsets.x == -100 && offsets.y == 200);
    offsets = DPT_ScrollFlatOffsets(DPT_SCROLL_CARRY, 100, 200);
    REQUIRE(offsets.x == 100 && offsets.y == 200);
    offsets = DPT_ScrollFlatOffsets(DPT_SCROLL_WALL, 100, 200);
    REQUIRE(offsets.x == 0 && offsets.y == 0);

    // The carry velocity is built from the RAW speed, not from the negated flat
    // offset: Boom passes (-dx, dy) to the flat scroller but (+dx, +dy) to the
    // carrier. The flat's coordinate moves by -dx, so the picture appears to move
    // by +dx, and that is where objects must ride. Using the offset here would
    // drag things against the visual, and a test that only checks "not zero"
    // would not notice.
    {
        dpt_scroll_t flat = DPT_ScrollFlatOffsets(DPT_SCROLL_FLOOR_CARRY, 100, 0);
        fixed_t carry_x = DPT_ScrollCarrySpeed(100);
        REQUIRE(flat.x == -100);            // the texture coordinate moves back
        REQUIRE(carry_x > 0);               // while things ride forward
        REQUIRE((flat.x < 0) != (carry_x < 0));
    }

    // Wall projection. Boom's formula is
    //   x = -(dy*l->dy + dx*l->dx)/len,  y = -(dx*l->dy - dy*l->dx)/len
    // which is the scroll vector dotted with the line direction (into x) and with
    // its perpendicular (into y), both negated. The signs are NOT symmetric, and
    // the values below are cross-checked against an independent re-implementation
    // of that formula in Python: an earlier version of this test asserted -1 where
    // Boom produces +1, so the test would have rejected correct code.
    {
        fixed_t one = FRACUNIT;
        fixed_t L = 64 * one;
        // Line along +x, scroll along +x: the parallel component, negated.
        offsets = DPT_ScrollWallOffsets(one, 0, L, 0);
        REQUIRE(offsets.x == -one);
        REQUIRE(offsets.y == 0);
        // Line along +x, scroll along +y: the perpendicular component, +1 here.
        offsets = DPT_ScrollWallOffsets(0, one, L, 0);
        REQUIRE(offsets.x == 0);
        REQUIRE(offsets.y == one);
        // Line along +y: the same two cases transpose with a flipped y sign.
        offsets = DPT_ScrollWallOffsets(one, 0, 0, L);
        REQUIRE(offsets.x == 0);
        REQUIRE(offsets.y == -one);
        offsets = DPT_ScrollWallOffsets(0, one, 0, L);
        REQUIRE(offsets.x == -one);
        REQUIRE(offsets.y == 0);
    }

    // A zero-length line has no direction to project onto: no scroll, rather than
    // a division by zero or a random direction.
    offsets = DPT_ScrollWallOffsets(FRACUNIT, FRACUNIT, 0, 0);
    REQUIRE(offsets.x == 0 && offsets.y == 0);

    // Magnitude preservation: on a 45-degree line, the scroll vector lying along
    // the line survives whole and contributes nothing across it. The projected
    // magnitude is the input's own length, s*sqrt(2) ~ 32764, not s itself -
    // cross-checked against the Python re-implementation of Boom's formula.
    {
        fixed_t s = 23170;   // FRACUNIT * sin(45) approximately, in 16.16
        dpt_scroll_t a = DPT_ScrollWallOffsets(s, s, s, s);
        REQUIRE(a.y == 0);
        REQUIRE(-a.x > 32760 && -a.x < 32770);
    }

    puts("Boom scrollers: type mapping, speed, carry factor, flat signs, wall projection OK");
    return 0;
}
