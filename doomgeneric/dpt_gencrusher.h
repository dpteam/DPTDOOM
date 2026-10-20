// Boom generalized crusher decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENCRUSHER_H
#define DPT_GENCRUSHER_H

#include "doomtype.h"

// Generalized crushers: 0x2f80..0x2fff. Generalized stairs start at 0x3000.
#define DPT_GEN_CRUSHER_BASE 0x2f80
#define DPT_GEN_CRUSHER_END  0x2fff

typedef struct
{
    int     trigger;      // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;  // 0..3 -> 1x, 2x, 4x, 8x CEILSPEED
    boolean monsters;     // monsters may activate
    boolean silent;       // no stnmov/pstop sounds
} dpt_gencrusher_t;

boolean DPT_GenCrusherIsCrusher(int special);
boolean DPT_GenCrusherDecode(int special, dpt_gencrusher_t *out);
boolean DPT_GenCrusherSupported(int special);

#endif
