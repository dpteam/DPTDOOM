// Boom generalized ceiling decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENCEILING_H
#define DPT_GENCEILING_H

#include "doomtype.h"

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
    int     target;           // DPT_CEILING_TARGET_*, or 0 for absolute height
    int     absolute_height;  // 24, 32, 40, ... when target == 0
    int     change;           // 0=none 1=zero type 2=texture 3=texture+type
    boolean crush;
    boolean monsters;         // monsters may activate (only when change == 0)
} dpt_genceiling_t;

boolean DPT_GenCeilingIsCeiling(int special);
boolean DPT_GenCeilingDecode(int special, dpt_genceiling_t *out);
boolean DPT_GenCeilingSupported(int special);

#endif
