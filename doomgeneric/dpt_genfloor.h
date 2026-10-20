// Boom generalized floor decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENFLOOR_H
#define DPT_GENFLOOR_H

#include "doomtype.h"

// Generalized floors live here; ceilings are the neighbouring 0x4000..0x5fff
// range and are not implemented.
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
    DPT_FLOOR_TARGET_ABSOLUTE = 7 // not used; absolute heights set target = 0
};

typedef struct
{
    int     trigger;          // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;      // 0..3 -> 1x, 2x, 4x, 8x FLOORSPEED
    boolean up;               // direct bit
    int     target;           // DPT_FLOOR_TARGET_*, or 0 for absolute height
    int     absolute_height;  // 24, 32, 40, ... when target == 0
    int     change;           // 0=none 1=zero type 2=texture 3=texture+type
    boolean crush;
    boolean monsters;         // monsters may activate (only when change == 0)
} dpt_genfloor_t;

boolean DPT_GenFloorIsFloor(int special);
boolean DPT_GenFloorDecode(int special, dpt_genfloor_t *out);
boolean DPT_GenFloorSupported(int special);

#endif
