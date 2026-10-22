// Boom generalized floor decoding and destination selection.
// GPL-2.0-or-later; see LICENSE.
//
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (1x/2x/4x/8x FLOORSPEED)
//   model    0x0020  (monsters may activate, unless a texture change is set)
//   direction 0x0040 (1=up)
//   target   0x0380  (0..7)
//   change   0x0c00  (0=none 1=zero type 2=texture 3=texture+type)
//   crush    0x1000
//
// Targets in BOOM's order: highest neighbour floor, lowest neighbour floor,
// next neighbour floor (direction dependent), lowest neighbour ceiling, own
// ceiling, shortest lower texture, by 24, by 32.
#ifndef DPT_GENFLOOR_H
#define DPT_GENFLOOR_H

#include "doomtype.h"
#include "m_fixed.h"

// Generalized floors: 0x6000..0x7fff.
#define DPT_GEN_FLOOR_BASE 0x6000
#define DPT_GEN_FLOOR_END  0x7fff

// BOOM neighbour targets, in BOOM's order.
enum
{
    DPT_FLOOR_TARGET_HNF = 1,   // highest neighbour floor
    DPT_FLOOR_TARGET_LNF = 2,   // lowest neighbour floor
    DPT_FLOOR_TARGET_NNF = 3,   // next neighbour floor
    DPT_FLOOR_TARGET_LNC = 4,   // lowest neighbour ceiling
    DPT_FLOOR_TARGET_CEIL = 5,  // ceiling
    DPT_FLOOR_TARGET_TEXTURE = 6, // shortest lower texture
    DPT_FLOOR_TARGET_ABSOLUTE = 7 // not used; relative heights set target = 0
};

typedef struct
{
    int     trigger;          // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;      // 0..3 -> 1x, 2x, 4x, 8x FLOORSPEED
    boolean up;               // direct bit
    int     target;           // DPT_FLOOR_TARGET_*, or 0 for a relative height
    int     relative_height;  // 24 or 32 when target == 0
    int     change;           // 0=none 1=zero type 2=texture 3=texture+type
    boolean crush;
    boolean monsters;         // monsters may activate (only when change == 0)
} dpt_genfloor_t;

boolean DPT_GenFloorIsFloor(int special);
boolean DPT_GenFloorDecode(int special, dpt_genfloor_t *out);
boolean DPT_GenFloorSupported(int special);

// Everything the destination depends on, gathered by the caller. Passing the
// neighbour heights in instead of a sector keeps the choice testable without a
// map: the mapping from target to height is where a wrong helper would hide.
typedef struct
{
    fixed_t current;            // this sector's floor height
    fixed_t highest_floor;      // highest surrounding floor
    fixed_t lowest_floor;       // lowest surrounding floor
    fixed_t next_lowest_floor;  // highest surrounding floor below current
    fixed_t next_highest_floor; // lowest surrounding floor above current
    fixed_t lowest_ceiling;     // lowest surrounding ceiling
    fixed_t ceiling;            // this sector's ceiling height
} dpt_floor_env_t;

// Destination height for a supported generalized floor, in 16.16 fixed point.
// Mirrors BOOM: the relative "by 24 / by 32" targets use the direction bit, and
// no crush offset is applied, because BOOM's generalized floors move exactly to
// their target height unlike the vanilla raiseFloorCrush type.
fixed_t DPT_GenFloorDest (const dpt_genfloor_t* gen, const dpt_floor_env_t* env);

#endif
