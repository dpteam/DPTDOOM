// Boom generalized crushers, bit decoding only.
// GPL-2.0-or-later; see LICENSE.
//
// Crushers have the simplest generalized encoding: no target and no crush bit,
// because every crusher crushes.
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (slow/normal/fast/turbo)
//   monster  0x0020
//   silent   0x0040
#include "dpt_gencrusher.h"

boolean DPT_GenCrusherIsCrusher(int special)
{
    return special >= DPT_GEN_CRUSHER_BASE && special <= DPT_GEN_CRUSHER_END;
}

boolean DPT_GenCrusherDecode(int special, dpt_gencrusher_t *out)
{
    if (!out || !DPT_GenCrusherIsCrusher(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_shift = (special & 0x0018) >> 3;
    out->monsters = (special & 0x0020) != 0;
    out->silent = (special & 0x0040) != 0;
    return true;
}

boolean DPT_GenCrusherSupported(int special)
{
    dpt_gencrusher_t crusher;

    if (!DPT_GenCrusherDecode(special, &crusher))
        return false;

    // Every trigger, including push, is dispatched; push acts on the sector
    // behind the line.
    (void) crusher;
    return true;
}
