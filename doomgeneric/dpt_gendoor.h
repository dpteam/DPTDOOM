// Boom generalized door decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENDOOR_H
#define DPT_GENDOOR_H

#include "doomtype.h"

// BOOM generalized doors occupy this range. Locked doors sit just below it in
// 0x3800..0x3bff and are not implemented.
#define DPT_GEN_DOOR_BASE 0x3c00
#define DPT_GEN_DOOR_END  0x3fff

typedef struct
{
    int     trigger;        // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;    // 0..3 -> 1x, 2x, 4x, 8x VDOORSPEED
    int     kind;           // 0=open/wait/close 1=open 2=close/wait/open 3=close
    boolean monsters;       // monsters may activate the line
    int     delay_seconds;  // 1, 4, 9 or 30
} dpt_gendoor_t;

boolean DPT_GenDoorIsDoor(int special);
boolean DPT_GenDoorDecode(int special, dpt_gendoor_t *out);

#endif
