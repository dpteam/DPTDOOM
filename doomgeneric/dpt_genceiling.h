// Boom generalized ceiling decoding and destination selection.
// GPL-2.0-or-later; see LICENSE.
//
// Same bit layout as generalized floors, but a different target order and a
// different meaning for the crush bit: for ceilings the crush only makes sense
// while lowering.
//
//   trigger  0x0007
//   speed    0x0018  (1x/2x/4x/8x CEILSPEED)
//   model    0x0020  (monsters may activate, unless a texture change is set)
//   direction 0x0040 (1=up)
//   target   0x0380  (0..7)
//   change   0x0c00
//   crush    0x1000
#ifndef DPT_GENCEILING_H
#define DPT_GENCEILING_H

#include "doomtype.h"
#include "m_fixed.h"

// Generalized ceilings: 0x4000..0x5fff.
#define DPT_GEN_CEILING_BASE 0x4000
#define DPT_GEN_CEILING_END  0x5fff

// BOOM neighbour targets for ceilings, in BOOM's order.
enum
{
    DPT_CEILING_TARGET_HNC = 1,   // highest neighbour ceiling
    DPT_CEILING_TARGET_LNC = 2,   // lowest neighbour ceiling
    DPT_CEILING_TARGET_NNC = 3,   // next neighbour ceiling
    DPT_CEILING_TARGET_HNF = 4,   // highest neighbour floor
    DPT_CEILING_TARGET_FLOOR = 5, // own floor
    DPT_CEILING_TARGET_TEXTURE = 6 // shortest upper texture
};

typedef struct
{
    int     trigger;          // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;      // 0..3 -> 1x, 2x, 4x, 8x CEILSPEED
    boolean up;               // direct bit
    int     target;           // DPT_CEILING_TARGET_*, or 0 for a relative height
    int     relative_height;  // 24 or 32 when target == 0
    int     change;           // 0=none 1=zero type 2=texture 3=texture+type
    boolean crush;
    boolean monsters;         // monsters may activate (only when change == 0)
} dpt_genceiling_t;

boolean DPT_GenCeilingIsCeiling(int special);
boolean DPT_GenCeilingDecode(int special, dpt_genceiling_t *out);
boolean DPT_GenCeilingSupported(int special);

typedef struct
{
    fixed_t current;         // this sector's ceiling height
    fixed_t highest_ceiling; // highest surrounding ceiling
    fixed_t lowest_ceiling;  // lowest surrounding ceiling
    fixed_t highest_floor;   // highest surrounding floor
    fixed_t floor;           // this sector's floor height
} dpt_ceiling_env_t;

// Destination height for a supported generalized ceiling, in 16.16 fixed point.
// No crush offset, matching BOOM: a crushing generalized ceiling moves to its
// target and stops there.
fixed_t DPT_GenCeilingDest (const dpt_genceiling_t* gen, const dpt_ceiling_env_t* env);

#endif
