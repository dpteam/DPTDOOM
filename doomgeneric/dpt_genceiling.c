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
        out->relative_height = 24 + (raw_target - 7) * 8;
    }
    else
    {
        out->target = raw_target;
        out->relative_height = 0;
    }

    out->monsters = out->change == 0 && (special & 0x0020) != 0;
    return true;
}

boolean DPT_GenCeilingSupported(int special)
{
    dpt_genceiling_t ceiling;

    if (!DPT_GenCeilingDecode(special, &ceiling))
        return false;

    // Texture/model changes need model-sector logic and are reported. Every
    // target that resolves to a height is supported now that the next-ceiling
    // and shortest-upper-texture helpers exist.
    if (ceiling.change != 0)
        return false;

    // Crushing only exists when lowering.
    if (ceiling.crush && ceiling.up)
        return false;

    return true;
}


fixed_t DPT_GenCeilingDest (const dpt_genceiling_t* gen, const dpt_ceiling_env_t* env)
{
    if (!gen || !env)
        return env ? env->current : 0;

    switch (gen->target)
    {
      case DPT_CEILING_TARGET_HNC:
        return env->highest_ceiling;

      case DPT_CEILING_TARGET_LNC:
        return env->lowest_ceiling;

      case DPT_CEILING_TARGET_NNC:
        // "Next" depends on direction, like the floor equivalent.
        return gen->up ? env->next_highest_ceiling : env->next_lowest_ceiling;

      case DPT_CEILING_TARGET_TEXTURE:
        // Move by the shortest upper texture around the sector, in the
        // direction of travel, clamped in whole units to +/-32000 so the height
        // cannot wrap - the same guard BOOM uses.
        {
            int delta = env->shortest_upper >> FRACBITS;
            int units = (env->current >> FRACBITS) + (gen->up ? delta : -delta);

            if (units > 32000)
                units = 32000;
            else if (units < -32000)
                units = -32000;
            return units << FRACBITS;
        }

      case DPT_CEILING_TARGET_HNF:
        return env->highest_floor;

      case DPT_CEILING_TARGET_FLOOR:
        return env->floor;

      case 0:
        // Relative by 24 or 32 units from the current height.
        return gen->up
            ? env->current + gen->relative_height * FRACUNIT
            : env->current - gen->relative_height * FRACUNIT;

      default:
        // Unsupported targets never reach the engine.
        return env->current;
    }
}