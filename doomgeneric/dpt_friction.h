// Boom variable friction (linedef 223), pure integer math. GPL-2.0-or-later.
//
// Boom derives a friction value from the *length* of a linedef 223, then applies
// it to every sector the line tags. The arithmetic is deliberately small but
// full of magic constants, and getting one of them wrong makes a map either
// frictionless or impossible to walk on - so it lives here, testable without a
// map or an engine.
//
// Two things about the numbers are easy to get backwards:
//
//   * A *higher* friction value means *less* friction. At movement time the
//     momentum is multiplied by friction/0x10000, so 0x10000 is frictionless and
//     0 would stop instantly. ORIG_FRICTION (0xE800) is the vanilla value.
//   * movefactor scales how fast the player accelerates on that floor, and it
//     runs the opposite way from friction: ice gets a large factor.
#ifndef DPT_FRICTION_H
#define DPT_FRICTION_H

#include "doomtype.h"
#include "m_fixed.h"

// Vanilla constants, from Boom's doomdef.h.
#define DPT_ORIG_FRICTION       0xE800
#define DPT_ORIG_FRICTION_FACTOR 2048

// Shortest and longest useful linedef 223 lengths, in map units. Boom computes
// friction from the length without a bound; lengths outside this range are
// clamped instead, so a stray zero-length or absurd line cannot produce a
// friction value that makes the floor unplayable.
#define DPT_FRICTION_MIN_LENGTH 1
#define DPT_FRICTION_MAX_LENGTH 200

typedef struct
{
    int friction;    // 16.16, ORIG_FRICTION when unmodified
    int movefactor;  // acceleration scale, ORIG_FRICTION_FACTOR when unmodified
} dpt_friction_t;

// Friction and movefactor for a linedef 223 of the given length (map units).
int DPT_FrictionForLength (int length, dpt_friction_t* out);

// True when the sector special carries the friction flag. Boom only honours the
// stored friction on sectors that are flagged, so a 223 line that tags an
// unflagged sector has no effect - which surprises mappers, and is Boom's
// behaviour, not ours to "fix".
boolean DPT_FrictionFlagged (int sector_special);

// Which of two flagged sectors wins when an object overlaps both: Boom takes the
// lower friction value, i.e. the slipperier floor. This engine only ever looks
// at the sector the object stands in, so the function exists for the documented
// rule and for the tests, not for a per-tic scan.
int DPT_FrictionSlipperiest (int first, int second);

#endif
