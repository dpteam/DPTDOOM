// Boom's generalized "change" field, shared by floors and ceilings.
// GPL-2.0-or-later; see LICENSE.
// No engine state: every function is a function of its argument.
#include "dpt_genchange.h"

int DPT_GenChangeEffect (int change)
{
    switch (change)
    {
      case 1:
        // Boom's FChgZero / CChgZero.
        return DPT_CHANGE_TEXTURE_ZERO_TYPE;
      case 2:
        // Boom's FChgTyp / CChgTyp.
        return DPT_CHANGE_TEXTURE_TYPE;
      case 3:
        // Boom's FChgTxt / CChgTxt.
        return DPT_CHANGE_TEXTURE;
      default:
        return DPT_CHANGE_NONE;
    }
}

boolean DPT_GenChangeNeedsSource (int effect)
{
    return effect != DPT_CHANGE_NONE;
}

boolean DPT_GenChangeWritesType (int effect)
{
    return effect == DPT_CHANGE_TEXTURE_TYPE
        || effect == DPT_CHANGE_TEXTURE_ZERO_TYPE;
}
