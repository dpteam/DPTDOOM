// Boom generalized sector specials, bit decoding. Data-free.
// GPL-2.0-or-later; see LICENSE.
#include "dpt_secspecial.h"

boolean DPT_SecSpecialIsGeneralized(int special)
{
    return special >= 32;
}

boolean DPT_SecSpecialDecode(int special, dpt_secspecial_t *out)
{
    if (!out)
        return false;

    // Bits above the ones BOOM defines are undefined. Reject rather than
    // silently drop them: a map using them expects behaviour we do not have.
    if (special & ~DPT_SEC_KNOWN_MASK)
        return false;

    out->base = special & DPT_SEC_BASE_MASK;
    out->damage = (special & DPT_SEC_DAMAGE_MASK) >> DPT_SEC_DAMAGE_SHIFT;
    out->secret = (special & DPT_SEC_SECRET_MASK) != 0;
    out->friction = (special & DPT_SEC_FRICTION_MASK) != 0;
    out->push = (special & DPT_SEC_PUSH_MASK) != 0;
    return true;
}

boolean DPT_SecSpecialSupported(int special)
{
    dpt_secspecial_t sec;

    if (!DPT_SecSpecialDecode(special, &sec))
        return false;

    // Friction and push are carried by thinkers this engine does not implement.
    // The base type, damage and secret parts are all implemented.
    return !sec.friction && !sec.push;
}

int DPT_SecSpecialDamage(int damage)
{
    switch (damage)
    {
      case 1:  return 5;
      case 2:  return 10;
      case 3:  return 20;
    }
    return 0;
}

boolean DPT_SecSpecialSuitBlocks(int damage)
{
    // Only the heaviest tier leaks through a radiation suit.
    return damage != 3;
}
