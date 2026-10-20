// Boom generalized locked doors, bit decoding and key requirements.
// GPL-2.0-or-later; see LICENSE.
//
// Layout per the PrBoom editing reference (BOOM's generalized linedefs):
//   trigger    0x0007  (0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR)
//   speed      0x0018  (slow/normal/fast/turbo)
//   kind       0x0020  (0=open/wait/close, 1=open and stay)
//   lock       0x01c0  (0=any, 1=red card, 2=blue card, 3=yellow card,
//                       4=red skull, 5=blue skull, 6=yellow skull, 7=all)
//   sk=ck      0x0200  (skull and card of the same colour are interchangeable)
//
// Only the two *open* kinds exist for locked doors, and monsters can never open
// them. Bit 0x0080 is not part of this range; ZDoom-derived code reuses it as a
// monster-activation flag, which BOOM locked doors do not have.
#include "dpt_genlocked.h"

boolean DPT_GenLockedIsLocked(int special)
{
    return special >= DPT_GEN_LOCKED_BASE && special <= DPT_GEN_LOCKED_END;
}

boolean DPT_GenLockedDecode(int special, dpt_genlocked_t *out)
{
    if (!out || !DPT_GenLockedIsLocked(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_shift = (special & 0x0018) >> 3;
    out->kind = (special & 0x0020) >> 5;
    out->lock = (special & 0x01c0) >> 6;
    out->skull_card = (special & 0x0200) != 0;
    return true;
}

boolean DPT_GenLockedSupported(int special)
{
    dpt_genlocked_t locked;

    // Every lock value 0..7 is implemented, so the whole range is supported.
    return DPT_GenLockedDecode(special, &locked);
}

boolean DPT_GenLockedSatisfied(int lock, boolean skull_card,
                              unsigned int held)
{
    unsigned int red = DPT_KEY_REDCARD | DPT_KEY_REDSKULL;
    unsigned int blue = DPT_KEY_BLUECARD | DPT_KEY_BLUESKULL;
    unsigned int yellow = DPT_KEY_YELLOWCARD | DPT_KEY_YELLOWSKULL;

    if (lock < DPT_LOCK_ANY || lock > DPT_LOCK_ALL)
        return false;

    if (lock == DPT_LOCK_ANY)
        return (held & DPT_KEY_ALL) != 0;

    if (lock == DPT_LOCK_ALL)
    {
        if (skull_card)
        {
            // One key of each colour, card or skull.
            return (held & red) && (held & blue) && (held & yellow);
        }
        return (held & DPT_KEY_ALL) == DPT_KEY_ALL;
    }

    switch (lock)
    {
      case DPT_LOCK_REDCARD:
        return (held & DPT_KEY_REDCARD)
            || (skull_card && (held & DPT_KEY_REDSKULL));
      case DPT_LOCK_BLUECARD:
        return (held & DPT_KEY_BLUECARD)
            || (skull_card && (held & DPT_KEY_BLUESKULL));
      case DPT_LOCK_YELLOWCARD:
        return (held & DPT_KEY_YELLOWCARD)
            || (skull_card && (held & DPT_KEY_YELLOWSKULL));
      case DPT_LOCK_REDSKULL:
        return (held & DPT_KEY_REDSKULL)
            || (skull_card && (held & DPT_KEY_REDCARD));
      case DPT_LOCK_BLUESKULL:
        return (held & DPT_KEY_BLUESKULL)
            || (skull_card && (held & DPT_KEY_BLUECARD));
      case DPT_LOCK_YELLOWSKULL:
        return (held & DPT_KEY_YELLOWSKULL)
            || (skull_card && (held & DPT_KEY_YELLOWCARD));
    }
    return false;
}
