// Boom generalized door bit decoding, isolated for testing.
// GPL-2.0-or-later; see LICENSE.
//
// Bit layout and trigger numbering follow BOOM's generalized linedef
// definitions (0x3c00..0x3fff): low 3 bits = trigger, bits 3-4 = speed,
// bits 5-6 = kind, bit 7 = monsters may activate, bits 8-9 = delay.
//
// This unit is deliberately free of engine state so a data-free test can verify
// every field combination against known BOOM values.
#include "dpt_gendoor.h"

boolean DPT_GenDoorIsDoor(int special)
{
    return special >= DPT_GEN_DOOR_BASE && special <= DPT_GEN_DOOR_END;
}

boolean DPT_GenDoorDecode(int special, dpt_gendoor_t *out)
{
    static const int delays[4] = {1, 4, 9, 30};

    if (!out || !DPT_GenDoorIsDoor(special))
        return false;

    out->trigger = special & 0x0007;
    out->speed_shift = (special & 0x0018) >> 3;
    out->kind = (special & 0x0060) >> 5;
    out->monsters = (special & 0x0080) != 0;
    out->delay_seconds = delays[(special & 0x0300) >> 8];
    return true;
}
