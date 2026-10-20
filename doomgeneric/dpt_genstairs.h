// Boom generalized stairs decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENSTAIRS_H
#define DPT_GENSTAIRS_H

#include "doomtype.h"

// Generalized stairs: 0x3000..0x33ff. Lifts start at 0x3400.
#define DPT_GEN_STAIRS_BASE 0x3000
#define DPT_GEN_STAIRS_END  0x33ff

typedef struct
{
    int     trigger;         // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_eighths;   // 2/4/16/32 eighths of a unit per tic
    boolean monsters;
    int     step;            // 4, 8, 16 or 24 units
    boolean up;              // direction
    boolean ignore_texture;  // do not stop at a different floor texture
} dpt_genstairs_t;

boolean DPT_GenStairsIsStairs(int special);
boolean DPT_GenStairsDecode(int special, dpt_genstairs_t *out);
boolean DPT_GenStairsSupported(int special);

#endif
