// DPT.DOOM match rules, intentionally not ZDoom/other ports' dmflags numbering.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_RULES_H
#define DPT_RULES_H
#include "d_player.h"
#include "p_mobj.h"

#define DPT_DMF_COOP_NO_EXTRA_WEAPONS 1u
#define DPT_DMF_PLAYERS_PASS_THROUGH  2u
#define DPT_DMF_SUPPORTED (DPT_DMF_COOP_NO_EXTRA_WEAPONS | DPT_DMF_PLAYERS_PASS_THROUGH)
extern unsigned int dpt_dmflags;

// Decimal-only, no sign or unknown bits; do not silently mask bad input.
static inline boolean DPT_ParseDMFlags(const char *text, unsigned int *result)
{
    unsigned int value = 0;
    const char *p;
    if (!text || !text[0]) return false;
    for (p = text; *p; ++p)
    {
        if (*p < '0' || *p > '9' || value > DPT_DMF_SUPPORTED / 10) return false;
        value = value * 10 + (unsigned int)(*p - '0');
        if (value > DPT_DMF_SUPPORTED) return false;
    }
    if (value & ~DPT_DMF_SUPPORTED) return false;
    *result = value;
    return true;
}

static inline boolean DPT_WeaponMapType(int doomednum)
{
    // Pickups, not MT_PLASMA/MT_BFG projectile types. Pistol is not a map pickup.
    return (doomednum >= 2001 && doomednum <= 2006) || doomednum == 82;
}

static inline boolean DPT_SkipCoopWeapon(unsigned int flags, boolean multiplayer,
                                        int dm, const mapthing_t *thing)
{
    return multiplayer && !dm && (flags & DPT_DMF_COOP_NO_EXTRA_WEAPONS)
        && (thing->options & 16) && DPT_WeaponMapType(thing->type);
}

static inline boolean DPT_PlayersPass(unsigned int flags, boolean multiplayer,
                                     const mobj_t *a, const mobj_t *b)
{
    return multiplayer && (flags & DPT_DMF_PLAYERS_PASS_THROUGH)
        && a != b && a->type == MT_PLAYER && b->type == MT_PLAYER
        && a->player && b->player && a->player->mo == a && b->player->mo == b
        && a->player->playerstate == PST_LIVE && b->player->playerstate == PST_LIVE
        && a->health > 0 && b->health > 0;
}
#endif
