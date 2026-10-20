// Boom generalized ceilings, bit decoding only.
// GPL-2.0-or-later; see LICENSE.
//
// Bit layout is the same as generalized floors (BOOM uses one scheme for both),
// but the target numbering and the meaning of "crush" differ:
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (slow/normal/fast/turbo)
//   model    0x0020  (numeric model, or "monsters may activate" when no change)
//   direct   0x0040  (0=down, 1=up)
//   target   0x0380  (1=HnC 2=LnC 3=NnC 4=HnF 5=F 6=sT, 7/8=absolute 24/32)
//   change   0x0c00  (0=none, 1=zero type, 2=texture, 3=texture+type)
//   crush    0x1000
#include "dpt_genceiling.h"

boolean DPT_GenCeilingIsCeiling(int special)
{
    return special >= DPT_GEN_CEILING_BASE && special <= DPT_GEN_CEILING_END;
}

boolean DPT_GenCeilingDecode(int special, dpt_genceiling_t *out)
{
    int raw_target;

    if (!out || !DPT_GenCeilingIsCeiling(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_shift = (special & 0x0018) >> 3;
    out->up = (special & 0x0040) != 0;
    out->change = (special & 0x0c00) >> 10;
    out->crush = (special & 0x1000) != 0;

    raw_target = ((special & 0x0380) >> 7) + 1;
    if (raw_target >= 7)
    {
        out->target = 0;
        out->absolute_height = 24 + (raw_target - 7) * 8;
    }
    else
    {
        out->target = raw_target;
        out->absolute_height = 0;
    }

    out->monsters = out->change == 0 && (special & 0x0020) != 0;
    return true;
}

boolean DPT_GenCeilingSupported(int special)
{
    dpt_genceiling_t ceiling;

    if (!DPT_GenCeilingDecode(special, &ceiling))
        return false;

    // Only mappings that exist in this engine are accepted. Ceiling movement is
    // limited to "raise to highest neighbour ceiling", "lower to floor" and
    // "lower to floor and crush"; neighbour-next/neighbour-highest targets and
    // any texture/model change are reported instead of approximated.
    if (ceiling.change != 0 || ceiling.absolute_height != 0)
        return false;
    if (ceiling.trigger >= 6)
        return false;

    if (ceiling.up)
        return ceiling.target == DPT_CEILING_TARGET_HNC && !ceiling.crush;

    if (ceiling.target != DPT_CEILING_TARGET_FLOOR)
        return false;
    // Lowering to the floor only crushes when the crush bit is set.
    return true;
}
