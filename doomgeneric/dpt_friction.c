// Boom variable friction (linedef 223), pure integer math. GPL-2.0-or-later.
// No engine state, no globals: every function is a function of its arguments.
#include "dpt_friction.h"

// Sector special bit 0x0100: "this sector's friction was set by linedef 223".
// Kept in sync with DPT_SEC_FRICTION_MASK in dpt_secspecial.h; duplicated here
// so this module stays independent of the sector-special decoder.
#define DPT_FRICTION_MASK 0x0100

int DPT_FrictionForLength (int length, dpt_friction_t* out)
{
    int friction, movefactor;

    if (!out)
        return 0;

    if (length < DPT_FRICTION_MIN_LENGTH)
        length = DPT_FRICTION_MIN_LENGTH;
    else if (length > DPT_FRICTION_MAX_LENGTH)
        length = DPT_FRICTION_MAX_LENGTH;

    // Boom: (0x1EB8 * length) / 0x80 + 0xD000.
    friction = (0x1EB8 * length) / 0x80 + 0xD000;

    // Above ORIG_FRICTION the floor is slippery (ice); below it is sticky.
    // The two branches are different formulas, not one with a sign.
    if (friction > DPT_ORIG_FRICTION)
        movefactor = ((0x10092 - friction) * 0x70) / 0x158;
    else
        movefactor = ((friction - 0xDB34) * 0x0A) / 0x80;

    // Boom's MBF-mode guard. Applied unconditionally here: without it a long
    // line yields friction above FRACUNIT (negative drag, i.e. acceleration) or
    // a movefactor that lets a player outrun the map, and those are not
    // behaviours worth reproducing.
    if (friction > FRACUNIT)
        friction = FRACUNIT;
    if (friction < 0)
        friction = 0;
    if (movefactor < 32)
        movefactor = 32;

    out->friction = friction;
    out->movefactor = movefactor;
    return 1;
}

boolean DPT_FrictionFlagged (int sector_special)
{
    return (sector_special & DPT_FRICTION_MASK) != 0;
}

int DPT_FrictionSlipperiest (int first, int second)
{
    // Lower value = less friction = more slide.
    return first < second ? first : second;
}
