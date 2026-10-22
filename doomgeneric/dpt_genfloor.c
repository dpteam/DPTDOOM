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
#include "dpt_genchange.h"

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
        out->relative_height = 24 + (raw_target - DPT_TARGET_FIRST_ABSOLUTE) * 8;
    }
    else
    {
        out->target = raw_target;
        out->relative_height = 0;
    }

    // The model bit doubles as the monster-activation flag when the action does
    // not change textures. With a texture change it selects the model sector
    // instead, so monsters cannot activate those lines.
    out->monsters = out->change == 0 && (special & 0x0020) != 0;
    out->model = out->change != 0 && (special & 0x0020) != 0;
    return true;
}

boolean DPT_GenFloorSupported(int special)
{
    dpt_genfloor_t floor;

    if (!DPT_GenFloorDecode(special, &floor))
        return false;

    // Texture/sector-type changes are supported: the effect comes from the
    // change field and the source sector is either the activating line's front
    // sector or, with the model bit, a sector at the destination height. Both are
    // resolved when the thinker is created.
    //
    // Crushing only exists as a raising floor.
    if (floor.crush && !floor.up)
        return false;

    return true;
}


fixed_t DPT_GenFloorDest (const dpt_genfloor_t* gen, const dpt_floor_env_t* env)
{
    if (!gen || !env)
        return env ? env->current : 0;

    switch (gen->target)
    {
      case DPT_FLOOR_TARGET_HNF:
        return env->highest_floor;

      case DPT_FLOOR_TARGET_LNF:
        return env->lowest_floor;

      case DPT_FLOOR_TARGET_NNF:
        // "Next" depends on direction: the nearest level above when rising,
        // below when lowering.
        return gen->up ? env->next_highest_floor : env->next_lowest_floor;

      case DPT_FLOOR_TARGET_LNC:
        return env->lowest_ceiling;

      case DPT_FLOOR_TARGET_CEIL:
        return env->ceiling;

      case DPT_FLOOR_TARGET_TEXTURE:
        // Move by the shortest lower texture around the sector, in the
        // direction of travel. BOOM does this in whole map units and clamps to
        // +/-32000 so the height cannot wrap; the clamp is applied to the
        // destination rather than to the offset so a sector already near the
        // limit stays put instead of jumping.
        {
            int delta = env->shortest_lower >> FRACBITS;
            int units = (env->current >> FRACBITS) + (gen->up ? delta : -delta);

            if (units > 32000)
                units = 32000;
            else if (units < -32000)
                units = -32000;
            return units << FRACBITS;
        }

      case 0:
        // Relative by 24 or 32 units from the current height.
        return gen->up
            ? env->current + gen->relative_height * FRACUNIT
            : env->current - gen->relative_height * FRACUNIT;

      default:
        // Unsupported targets never reach the engine; returning the current
        // height keeps a mistake harmless instead of teleporting the floor.
        return env->current;
    }
}