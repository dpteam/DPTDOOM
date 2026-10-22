// Boom's generalized "change" field, shared by floors and ceilings.
// GPL-2.0-or-later; see LICENSE.
//
// Both families use the same numbering in Boom (floorchange_e and
// ceilingchange_e are identical), and in both the numbers are NOT in the order
// of their effects:
//
//   field 0  no change
//   field 1  texture, and the sector type is ZEROED
//   field 2  texture, and the sector type is COPIED from the source sector
//   field 3  texture only, type left alone
//
// Getting 1 and 3 the wrong way round is silent: the floor still moves and the
// texture still changes, only the sector type differs - which is exactly the kind
// of bug a map author would report as "the secret sector disappeared". So the
// mapping lives here, in one place, tested from both decoder tests.
#ifndef DPT_GENCHANGE_H
#define DPT_GENCHANGE_H

#include "doomtype.h"

// Effect on the sector when the move reaches its destination.
enum
{
    DPT_CHANGE_NONE = 0,              // leave both texture and type alone
    DPT_CHANGE_TEXTURE = 1,           // new texture, type untouched
    DPT_CHANGE_TEXTURE_TYPE = 2,      // new texture and the source's type
    DPT_CHANGE_TEXTURE_ZERO_TYPE = 3  // new texture, type set to 0
};

// Maps the raw change field (0..3) to its effect. Anything outside 0..3 is
// treated as "no change" rather than guessed.
int DPT_GenChangeEffect (int change);

// True when the effect needs a source sector to copy texture (and maybe type)
// from. False only for DPT_CHANGE_NONE.
boolean DPT_GenChangeNeedsSource (int effect);

// True when the effect also writes the sector type.
boolean DPT_GenChangeWritesType (int effect);

#endif
