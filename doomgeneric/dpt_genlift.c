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
    // BOOM's wait values are 1/3/5/10 seconds expressed in tics (35 tics per
    // second), i.e. 35/105/175/350. An earlier version of this table used
    // 8/24/40/80, which made every generalized lift pause about four times
    // shorter than BOOM - visible as a platform that barely settles before
    // rising again.
    static const int delays[4] = {35, 105, 175, 350};
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

    // Targets 1..3 all use the same down/wait/up platform; only the destination
    // height differs, and this engine computes that itself. Target 4 (perpetual
    // lowest<->highest) needs a platform that reverses forever, which is a
    // different movement type, so it is reported rather than approximated.
    if (lift.target == DPT_LIFT_TARGET_PERPETUAL)
        return false;

    // Every trigger, including push, is dispatched; push acts on the sector
    // behind the line.
    (void) lift;
    return true;
}


fixed_t DPT_GenLiftDest (const dpt_genlift_t* gen, const dpt_lift_env_t* env)
{
    fixed_t low;

    if (!gen || !env)
        return env ? env->current : 0;

    switch (gen->target)
    {
      case DPT_LIFT_TARGET_NNLF:
        // BOOM does not clamp this one: with nothing lower, the target equals
        // the current height and the platform simply does not travel.
        return env->next_lowest_floor;

      case DPT_LIFT_TARGET_LNC:
        low = env->lowest_ceiling;
        return low > env->current ? env->current : low;

      case DPT_LIFT_TARGET_LNF:
      default:
        low = env->lowest_floor;
        return low > env->current ? env->current : low;
    }
}