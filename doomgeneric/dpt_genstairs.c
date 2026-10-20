// Boom generalized stairs, bit decoding only.
// GPL-2.0-or-later; see LICENSE.
//
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (slow/normal/fast/turbo)
//   monster  0x0020
//   step     0x00c0  (4/8/16/24 units)
//   dir      0x0100  (0=down, 1=up)
//   igntxt   0x0200  (ignore floor texture when finding the next step)
//
// Speeds are NOT powers of two. csDoomOld07's p_xlat.cpp documents them as
// eighths of a unit per tic: S_SLOW=2, S_NORMAL=4, S_FAST=16, S_TURBO=32.
// That gives FLOORSPEED/4 and FLOORSPEED*4 at the ends, matching the vanilla
// build8 and turbo16 stair builders exactly.
#include "dpt_genstairs.h"

boolean DPT_GenStairsIsStairs(int special)
{
    return special >= DPT_GEN_STAIRS_BASE && special <= DPT_GEN_STAIRS_END;
}

boolean DPT_GenStairsDecode(int special, dpt_genstairs_t *out)
{
    static const int steps[4] = {4, 8, 16, 24};
    // eighths of a unit per tic, as documented by csDoomOld07 p_xlat.cpp
    static const int speeds[4] = {2, 4, 16, 32};

    if (!out || !DPT_GenStairsIsStairs(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_eighths = speeds[(special & 0x0018) >> 3];
    out->monsters = (special & 0x0020) != 0;
    out->step = steps[(special & 0x00c0) >> 6];
    out->up = (special & 0x0100) != 0;
    out->ignore_texture = (special & 0x0200) != 0;
    return true;
}

boolean DPT_GenStairsSupported(int special)
{
    dpt_genstairs_t stairs;

    if (!DPT_GenStairsDecode(special, &stairs))
        return false;

    // Push triggers would have to act on the sector behind the line; that path
    // is not wired for stairs, so reject instead of leaving the line inert.
    return stairs.trigger < 6;
}
