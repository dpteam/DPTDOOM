// Boom pusher sectors: wind and current (linedefs 224/225). GPL-2.0-or-later.
// No engine state: every function is a function of its arguments.
#include "dpt_push.h"

// Sector special bit 0x0200: "this sector has a pusher". Kept in sync with
// DPT_SEC_PUSH_MASK in dpt_secspecial.h, duplicated so this module stays
// independent of the sector-special decoder.
#define DPT_PUSH_MASK 0x0200

dpt_push_t DPT_PushForce (dpt_pushtype_t type, boolean above_ground,
                          int x_mag, int y_mag)
{
    dpt_push_t force;

    force.x = 0;
    force.y = 0;

    switch (type)
    {
      case DPT_PUSH_WIND:
        // Full force in the air, half on the floor. Boom halves with an
        // arithmetic shift, which rounds toward negative infinity; the division
        // below matches that for the negative axis values a reversed line has.
        force.x = above_ground ? x_mag : (x_mag >> 1);
        force.y = above_ground ? y_mag : (y_mag >> 1);
        break;

      case DPT_PUSH_CURRENT:
        // A current only acts on things that are in it.
        if (!above_ground)
        {
            force.x = x_mag;
            force.y = y_mag;
        }
        break;

      case DPT_PUSH_NONE:
      default:
        break;
    }
    return force;
}

int DPT_PushMagnitude (int delta)
{
    // Boom: `p->x_mag = x_mag >> FRACBITS`. A plain arithmetic shift, so a
    // negative direction stays negative.
    return delta >> 16;
}

dpt_pushtype_t DPT_PushTypeForLine (int special)
{
    switch (special)
    {
      case 224: return DPT_PUSH_WIND;
      case 225: return DPT_PUSH_CURRENT;
      default:  return DPT_PUSH_NONE;
    }
}

boolean DPT_PushFlagged (int sector_special)
{
    return (sector_special & DPT_PUSH_MASK) != 0;
}
