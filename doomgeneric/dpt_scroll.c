// Boom scrollers: floor/ceiling flat scroll, object carrying, wall scroll.
// GPL-2.0-or-later; see LICENSE.
// No engine state: every function is a function of its arguments.
#include "dpt_scroll.h"

typedef long long dpt_i64;
typedef unsigned long long dpt_u64;

dpt_scroll_kind_t DPT_ScrollKindForLine (int special)
{
    switch (special)
    {
      case 250: return DPT_SCROLL_CEILING;
      case 251: return DPT_SCROLL_FLOOR;
      case 252: return DPT_SCROLL_CARRY;
      case 253: return DPT_SCROLL_FLOOR_CARRY;
      case 254: return DPT_SCROLL_WALL;
      case 48:
      case 85:  return DPT_SCROLL_WALL_FIRST;
      default:  return DPT_SCROLL_NONE;
    }
}

boolean DPT_ScrollNeedsState (int special)
{
    // 214-218 accelerate; 245-249 are driven by another sector's height change.
    return (special >= 214 && special <= 218)
        || (special >= 245 && special <= 249);
}

fixed_t DPT_ScrollSpeed (fixed_t delta)
{
    // Boom: `l->dx >> SCROLL_SHIFT`. A plain arithmetic shift, so a line drawn
    // the other way scrolls the other way.
    return delta >> DPT_SCROLL_SHIFT;
}

fixed_t DPT_ScrollCarrySpeed (fixed_t speed)
{
    // FixedMul(speed, CARRYFACTOR), spelled out to keep this module free of the
    // engine's fixed-point helpers.
    return (fixed_t)(((dpt_i64)speed * DPT_SCROLL_CARRY_FACTOR) >> 16);
}

dpt_scroll_t DPT_ScrollFlatOffsets (dpt_scroll_kind_t kind, fixed_t speed_x,
                                    fixed_t speed_y)
{
    dpt_scroll_t offsets;

    switch (kind)
    {
      case DPT_SCROLL_CEILING:
      case DPT_SCROLL_FLOOR:
      case DPT_SCROLL_FLOOR_CARRY:
        // Boom passes (-dx, dy) to the flat scroller.
        offsets.x = -speed_x;
        offsets.y = speed_y;
        break;

      case DPT_SCROLL_CARRY:
        // Carrying uses the vector unnegated; the sign was already applied when
        // the flat scroller of a 253 was created.
        offsets.x = speed_x;
        offsets.y = speed_y;
        break;

      default:
        offsets.x = 0;
        offsets.y = 0;
        break;
    }
    return offsets;
}

static dpt_u64 IntegerSqrt (dpt_u64 value)
{
    // Newton's method on integers. Only used once per wall scroller at level
    // load, so clarity beats a table.
    dpt_u64 guess = value;
    dpt_u64 next;

    if (!value)
        return 0;
    // Start above the root so the iteration descends monotonically.
    if (guess < 2)
        return guess;
    do
    {
        next = (guess + value / guess) / 2;
        if (next >= guess)
            break;
        guess = next;
    } while (1);
    return guess;
}

dpt_scroll_t DPT_ScrollWallOffsets (fixed_t speed_x, fixed_t speed_y,
                                    fixed_t line_dx, fixed_t line_dy)
{
    dpt_scroll_t offsets;
    dpt_u64 squared;
    dpt_u64 length;

    offsets.x = 0;
    offsets.y = 0;

    // The line's length, in the same 16.16 units as its delta. Computed from the
    // squared lengths so no information is lost before the square root.
    squared = (dpt_u64)((dpt_i64)line_dx * line_dx)
            + (dpt_u64)((dpt_i64)line_dy * line_dy);
    length = IntegerSqrt(squared);
    if (!length)
        return offsets;     // zero-length line: nothing to project onto

    // Boom: x = -(dy*line_dy + dx*line_dx)/len, y = -(dx*line_dy - dy*line_dx)/len
    // i.e. the scroll vector dotted with the line direction and with its
    // perpendicular, both negated. 64-bit intermediates: two 16.16 values
    // multiplied together do not fit in 32 bits.
    offsets.x = (fixed_t)(-(((dpt_i64)speed_y * line_dy
                             + (dpt_i64)speed_x * line_dx) / (dpt_i64)length));
    offsets.y = (fixed_t)(-(((dpt_i64)speed_x * line_dy
                             - (dpt_i64)speed_y * line_dx) / (dpt_i64)length));
    return offsets;
}
