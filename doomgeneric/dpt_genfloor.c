// Boom generalized floors and ceilings, bit decoding only.
// GPL-2.0-or-later; see LICENSE.
//
// Bit layout follows BOOM's generalized linedef definitions:
//   trigger  0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed    0x0018  (slow/normal/fast/turbo)
//   model    0x0020  (numeric model, or "monsters may activate" when no change)
//   direct   0x0040  (0=down, 1=up)
//   target   0x0380  (1..6 neighbour targets, 7/8 absolute 24/32)
//   change   0x0c00  (0=none, 1=zero type, 2=texture, 3=texture+type)
//   crush    0x1000
//
// The neighbour target numbering (1..8) is BOOM's, not ours: it is derived as
// ((special & 0x0380) >> 7) + 1, and indices >= 7 become absolute heights
// 24, 32, ... in steps of 8. Keeping the raw numbering here makes the decoder
// testable against BOOM's documented values.
#include "dpt_genfloor.h"

// BOOM target order: HnF, LnF, NnF, LnC, C, sT, then absolute heights.
#define DPT_TARGET_FIRST 1
#define DPT_TARGET_LAST_NEIGHBOUR 6
#define DPT_TARGET_FIRST_ABSOLUTE 7

boolean DPT_GenFloorIsFloor(int special)
{
    return special >= DPT_GEN_FLOOR_BASE && special <= DPT_GEN_FLOOR_END;
}

boolean DPT_GenFloorDecode(int special, dpt_genfloor_t *out)
{
    int raw_target;

    if (!out || !DPT_GenFloorIsFloor(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_shift = (special & 0x0018) >> 3;
    out->up = (special & 0x0040) != 0;
    out->change = (special & 0x0c00) >> 10;
    out->crush = (special & 0x1000) != 0;

    raw_target = ((special & 0x0380) >> 7) + DPT_TARGET_FIRST;
    if (raw_target >= DPT_TARGET_FIRST_ABSOLUTE)
    {
        out->target = 0;
        out->absolute_height = 24 + (raw_target - DPT_TARGET_FIRST_ABSOLUTE) * 8;
    }
    else
    {
        out->target = raw_target;
        out->absolute_height = 0;
    }

    // The model bit doubles as the monster-activation flag when the action does
    // not change textures. With a texture change it selects the model sector
    // instead, so monsters cannot activate those lines.
    out->monsters = out->change == 0 && (special & 0x0020) != 0;
    return true;
}

boolean DPT_GenFloorSupported(int special)
{
    dpt_genfloor_t floor;

    if (!DPT_GenFloorDecode(special, &floor))
        return false;

    // Only the subset that maps exactly onto an existing floor behaviour is
    // supported. Texture/sector-type changes and absolute heights would need
    // model-sector logic or new movement types, so they are reported instead of
    // being approximated.
    if (floor.change != 0 || floor.absolute_height != 0)
        return false;
    if (floor.crush && !floor.up)
        return false;

    if (!floor.up)
        return floor.target == DPT_FLOOR_TARGET_HNF
            || floor.target == DPT_FLOOR_TARGET_LNF;

    return floor.target == DPT_FLOOR_TARGET_NNF
        || floor.target == DPT_FLOOR_TARGET_LNC;
}
