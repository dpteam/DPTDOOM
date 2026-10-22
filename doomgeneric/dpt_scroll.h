// Boom scrollers: floor/ceiling flat scroll, object carrying, wall scroll.
// GPL-2.0-or-later; see LICENSE.
//
// All the plain scrollers are constant: the speed and direction come from the
// activating linedef, so they are map data like friction and pushers - computed
// at level load, applied every tic, never archived. Two Boom families are NOT
// constant and are deliberately left out:
//
//   214-218  accelerative scrollers: they accumulate velocity, so they need
//            per-scroller state in the savegame.
//   245-249  displacement scrollers: the scroll amount depends on another
//            sector's height changing, which needs a control sector and a
//            remembered previous height.
//
// Both are reported by the map compatibility line rather than approximated.
//
// The sign conventions below are Boom's and they are not uniform, which is why
// they live here instead of being spread across the engine:
//
//   floor / ceiling   x is negated, y is not
//   carrying          neither is negated
//   wall (254)        both are negated, after projecting onto the line
#ifndef DPT_SCROLL_H
#define DPT_SCROLL_H

#include "doomtype.h"
#include "m_fixed.h"

// Boom shifts the linedef delta right by this much to get the per-tic speed.
#define DPT_SCROLL_SHIFT 5

// 3/32, in 16.16: the carry factor, chosen so objects riding a scrolling floor
// move at the same visible speed as the flat itself.
#define DPT_SCROLL_CARRY_FACTOR 6144

typedef enum
{
    DPT_SCROLL_NONE = 0,
    DPT_SCROLL_CEILING,       // 250
    DPT_SCROLL_FLOOR,         // 251
    DPT_SCROLL_CARRY,         // 252
    DPT_SCROLL_FLOOR_CARRY,   // 253
    DPT_SCROLL_WALL,          // 254, by tag
    DPT_SCROLL_WALL_FIRST     // 48 / 85, first side only
} dpt_scroll_kind_t;

typedef struct
{
    fixed_t x;
    fixed_t y;
} dpt_scroll_t;

// Kind of scroller a linedef special creates. 214-218 and 245-249 map to
// DPT_SCROLL_NONE here; use DPT_ScrollNeedsState to tell "not a scroller" from
// "a scroller this engine cannot do without state".
dpt_scroll_kind_t DPT_ScrollKindForLine (int special);

// True for the two families that need per-scroller state (214-218, 245-249).
// Reported separately so a map using them does not look supported.
boolean DPT_ScrollNeedsState (int special);

// Per-tic speed along one axis from the linedef delta, in fixed point.
fixed_t DPT_ScrollSpeed (fixed_t delta);

// Speed applied to things standing on a scrolling floor.
fixed_t DPT_ScrollCarrySpeed (fixed_t speed);

// Flat scroll offsets for a sector scroller, applying Boom's sign rules.
dpt_scroll_t DPT_ScrollFlatOffsets (dpt_scroll_kind_t kind, fixed_t speed_x,
                                    fixed_t speed_y);

// Wall scroll offsets for linedef 254: the scroll vector is projected onto the
// activating line's direction and its perpendicular, then negated. Integer
// square root is used instead of Boom's tantoangle tables; the result agrees to
// within the fixed-point rounding that matters here.
dpt_scroll_t DPT_ScrollWallOffsets (fixed_t speed_x, fixed_t speed_y,
                                    fixed_t line_dx, fixed_t line_dy);

#endif
