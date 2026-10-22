// Boom pusher sectors: wind and current (linedefs 224/225). GPL-2.0-or-later.
//
// These two are pure sector properties - a constant direction and strength
// derived from the activating line - so they need no per-thing state and no
// thinker, unlike the point-source push/pull variant (linedef 226) which needs
// MT_PUSH/MT_PULL things this engine does not have.
//
// The height rule is what distinguishes them, and it is easy to get backwards:
//
//   wind     above ground: full force   on the ground: half force
//   current  above ground: no force     on the ground: full force
//
// Wind pushes you whether you stand or jump; a current only drags you when you
// are in it. Boom applies a third case for "special water sectors" (a Boom
// feature keyed on a sector's water height), which this engine does not have, so
// only the two ordinary cases exist here.
#ifndef DPT_PUSH_H
#define DPT_PUSH_H

#include "doomtype.h"

typedef enum
{
    DPT_PUSH_NONE = 0,
    DPT_PUSH_WIND = 1,      // linedef 224
    DPT_PUSH_CURRENT = 2    // linedef 225
} dpt_pushtype_t;

typedef struct
{
    int x;      // map units per push tick, per axis
    int y;
} dpt_push_t;

// Force applied to a thing at the given height, in whole map units per axis.
// `x_mag`/`y_mag` are the line's direction in map units, so the caller does not
// have to remember the fixed-point conversion.
dpt_push_t DPT_PushForce (dpt_pushtype_t type, boolean above_ground,
                          int x_mag, int y_mag);

// The per-axis magnitudes Boom derives from a linedef's dx/dy: whole map units.
int DPT_PushMagnitude (int delta);

// Which pusher a linedef special creates, or DPT_PUSH_NONE for anything else.
// Only 224 and 225: 226 is the point-source variant, deliberately not mapped
// here so a caller cannot silently treat it as wind.
dpt_pushtype_t DPT_PushTypeForLine (int special);

// True when the sector special carries the push flag. Boom only runs a pusher
// on sectors flagged this way, so a 224 line that tags an unflagged sector does
// nothing - which surprises mappers, and is Boom's behaviour.
boolean DPT_PushFlagged (int sector_special);

#endif
