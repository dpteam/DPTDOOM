// Boom generalized lifts (platforms), bit decoding only.
// GPL-2.0-or-later; see LICENSE.
//
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (slow/normal/fast/turbo)
//   monster  0x0020
//   delay    0x00c0  (8/24/40/80 tics)
//   target   0x0300  (1..4)
//
// Speeds are documented in csDoomOld07's p_xlat.cpp as P_SLOW=8, P_NORMAL=16,
// P_FAST=32, P_TURBO=64 (eighths of a unit per tic), which is a clean doubling
// here - unlike stairs, whose table skips 8.
#include "dpt_genlift.h"

boolean DPT_GenLiftIsLift(int special)
{
    return special >= DPT_GEN_LIFT_BASE && special <= DPT_GEN_LIFT_END;
}

boolean DPT_GenLiftDecode(int special, dpt_genlift_t *out)
{
    static const int delays[4] = {8, 24, 40, 80};
    static const int speeds[4] = {8, 16, 32, 64};

    if (!out || !DPT_GenLiftIsLift(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_eighths = speeds[(special & 0x0018) >> 3];
    out->monsters = (special & 0x0020) != 0;
    out->delay_tics = delays[(special & 0x00c0) >> 6];
    out->target = ((special & 0x0300) >> 8) + 1;
    return true;
}

boolean DPT_GenLiftSupported(int special)
{
    dpt_genlift_t lift;

    if (!DPT_GenLiftDecode(special, &lift))
        return false;

    // Only "lowest neighbour floor" (BOOM target 1) maps onto this engine's
    // downWaitUpStay platform. The other targets (next lowest floor, lowest
    // neighbour ceiling, perpetual lowest<->highest) need movement types the
    // engine does not have, so they are reported rather than approximated.
    if (lift.target != DPT_LIFT_TARGET_LNF)
        return false;

    // Every trigger, including push, is dispatched; push acts on the sector
    // behind the line.
    (void) lift;
    return true;
}
