//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:  Ceiling aninmation (lowering, crushing, raising)
//



#include "z_zone.h"
#include "doomdef.h"
#include "dpt_genceiling.h"
#include "dpt_gencrusher.h"
#include "p_local.h"

#include "s_sound.h"

// State.
#include "doomstat.h"
#include "r_state.h"

// Data.
#include "sounds.h"

//
// CEILINGS
//


ceiling_t*	activeceilings[MAXCEILINGS];


//
// T_MoveCeiling
//

void T_MoveCeiling (ceiling_t* ceiling)
{
    result_e	res;
	
    switch(ceiling->direction)
    {
      case 0:
	// IN STASIS
	break;
      case 1:
	// UP
	res = T_MovePlane(ceiling->sector,
			  ceiling->speed,
			  ceiling->topheight,
			  false,1,ceiling->direction);
	
	if (!(leveltime&7))
	{
	    switch(ceiling->type)
	    {
	      case silentCrushAndRaise:
		break;
	      default:
		S_StartSound(&ceiling->sector->soundorg, sfx_stnmov);
		// ?
		break;
	    }
	}
	
	if (res == pastdest)
	{
	    switch(ceiling->type)
	    {
	      case raiseToHighest:
		P_RemoveActiveCeiling(ceiling);
		break;
		
	      case silentCrushAndRaise:
		S_StartSound(&ceiling->sector->soundorg, sfx_pstop);
	      case fastCrushAndRaise:
	      case crushAndRaise:
		ceiling->direction = -1;
		break;
		
	      default:
		break;
	    }
	    
	}
	break;
	
      case -1:
	// DOWN
	res = T_MovePlane(ceiling->sector,
			  ceiling->speed,
			  ceiling->bottomheight,
			  ceiling->crush,1,ceiling->direction);
	
	if (!(leveltime&7))
	{
	    switch(ceiling->type)
	    {
	      case silentCrushAndRaise: break;
	      default:
		S_StartSound(&ceiling->sector->soundorg, sfx_stnmov);
	    }
	}
	
	if (res == pastdest)
	{
	    switch(ceiling->type)
	    {
	      case silentCrushAndRaise:
		S_StartSound(&ceiling->sector->soundorg, sfx_pstop);
	      case crushAndRaise:
		ceiling->speed = ceiling->basespeed
			       ? ceiling->basespeed : CEILSPEED;
	      case fastCrushAndRaise:
		ceiling->direction = 1;
		break;

	      case lowerAndCrush:
	      case lowerToFloor:
		P_RemoveActiveCeiling(ceiling);
		break;

	      default:
		break;
	    }
	}
	else // ( res != pastdest )
	{
	    if (res == crushed)
	    {
		switch(ceiling->type)
		{
		  case silentCrushAndRaise:
		  case crushAndRaise:
		  case lowerAndCrush:
		    ceiling->speed = (ceiling->basespeed
				      ? ceiling->basespeed : CEILSPEED) / 8;
		    break;

		  default:
		    break;
		}
	    }
	}
	break;
    }
}


//
// EV_DoCeiling
// Move a ceiling up/down and all around!
//
int
EV_DoCeiling
( line_t*	line,
  ceiling_e	type )
{
    int		secnum;
    int		rtn;
    sector_t*	sec;
    ceiling_t*	ceiling;
	
    secnum = -1;
    rtn = 0;
    
    //	Reactivate in-stasis ceilings...for certain types.
    switch(type)
    {
      case fastCrushAndRaise:
      case silentCrushAndRaise:
      case crushAndRaise:
	P_ActivateInStasisCeiling(line);
      default:
	break;
    }
	
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	sec = &sectors[secnum];
	if (sec->specialdata)
	    continue;
	
	// new door thinker
	rtn = 1;
	ceiling = Z_Malloc (sizeof(*ceiling), PU_LEVSPEC, 0);
	memset(ceiling, 0, sizeof(*ceiling));
	P_AddThinker (&ceiling->thinker);
	sec->specialdata = ceiling;
	ceiling->thinker.function.acp1 = (actionf_p1)T_MoveCeiling;
	ceiling->sector = sec;
	ceiling->crush = false;
	
	switch(type)
	{
	  case fastCrushAndRaise:
	    ceiling->crush = true;
	    ceiling->topheight = sec->ceilingheight;
	    ceiling->bottomheight = sec->floorheight + (8*FRACUNIT);
	    ceiling->direction = -1;
	    ceiling->speed = CEILSPEED * 2;
	    break;

	  case silentCrushAndRaise:
	  case crushAndRaise:
	    ceiling->crush = true;
	    ceiling->topheight = sec->ceilingheight;
	  case lowerAndCrush:
	  case lowerToFloor:
	    ceiling->bottomheight = sec->floorheight;
	    if (type != lowerToFloor)
		ceiling->bottomheight += 8*FRACUNIT;
	    ceiling->direction = -1;
	    ceiling->speed = CEILSPEED;
	    break;

	  case raiseToHighest:
	    ceiling->topheight = P_FindHighestCeilingSurrounding(sec);
	    ceiling->direction = 1;
	    ceiling->speed = CEILSPEED;
	    break;
	}
		
	ceiling->tag = sec->tag;
	ceiling->type = type;
	P_AddActiveCeiling(ceiling);
    }
    return rtn;
}


//
// Start one generalized ceiling on a single sector. Returns 1 when a thinker was
// created. Shared by the tag loop and the push-trigger path.
//
static int
GenCeilingOnSector
( sector_t*		sec,
  const dpt_genceiling_t*	gen )
{
    ceiling_t*		ceiling;

    if (sec->specialdata)
	return 0;

    ceiling = Z_Malloc (sizeof(*ceiling), PU_LEVSPEC, 0);
    memset(ceiling, 0, sizeof(*ceiling));
    P_AddThinker (&ceiling->thinker);
    sec->specialdata = ceiling;
    ceiling->thinker.function.acp1 = (actionf_p1)T_MoveCeiling;
    ceiling->sector = sec;
    ceiling->crush = false;
    ceiling->speed = CEILSPEED << gen->speed_shift;

    // Destination comes from the tested pure function; the engine only gathers
    // the neighbour heights it needs.
    {
	dpt_ceiling_env_t env;
	env.current = sec->ceilingheight;
	env.highest_ceiling = P_FindHighestCeilingSurrounding(sec);
	env.lowest_ceiling = P_FindLowestCeilingSurrounding(sec);
	env.highest_floor = P_FindHighestFloorSurrounding(sec);
	env.floor = sec->floorheight;
	if (gen->up)
	{
	    ceiling->type = raiseToHighest;
	    ceiling->direction = 1;
	    ceiling->topheight = DPT_GenCeilingDest(gen, &env);
	}
	else
	{
	    ceiling->type = gen->crush ? lowerAndCrush : lowerToFloor;
	    ceiling->crush = gen->crush;
	    ceiling->direction = -1;
	    ceiling->bottomheight = DPT_GenCeilingDest(gen, &env);
	}
    }

    ceiling->tag = sec->tag;
    P_AddActiveCeiling(ceiling);
    return 1;
}

//
// Boom generalized ceiling.
//
// Only the supported subset is accepted (see DPT_GenCeilingSupported): raise to
// the highest neighbour ceiling, or lower to the floor with an optional crush.
// The speed comes from the generalized bits instead of the fixed CEILSPEED.
//
int
EV_DoGenCeiling
( line_t*	line )
{
    int			secnum;
    int			rtn;
    dpt_genceiling_t	gen;

    if (!DPT_GenCeilingDecode(line->special, &gen)
        || !DPT_GenCeilingSupported(line->special))
        return 0;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
	if (GenCeilingOnSector(&sectors[secnum], &gen))
	    rtn = 1;
    return rtn;
}

//
// Push-trigger entry point: the generalized ceiling applies to the sector behind
// the line, because BOOM treats push as a switch with a zero tag.
//
int
EV_DoGenCeilingSector
( sector_t*	sec,
  int		special )
{
    dpt_genceiling_t	gen;

    if (!sec
        || !DPT_GenCeilingDecode(special, &gen)
        || !DPT_GenCeilingSupported(special))
        return 0;
    return GenCeilingOnSector(sec, &gen);
}


//
// Start one generalized crusher on a single sector.
//
static int
GenCrusherOnSector
( sector_t*		sec,
  const dpt_gencrusher_t*	gen )
{
    ceiling_t*		ceiling;

    if (sec->specialdata)
	return 0;

    ceiling = Z_Malloc (sizeof(*ceiling), PU_LEVSPEC, 0);
    memset(ceiling, 0, sizeof(*ceiling));
    P_AddThinker (&ceiling->thinker);
    sec->specialdata = ceiling;
    ceiling->thinker.function.acp1 = (actionf_p1)T_MoveCeiling;
    ceiling->sector = sec;
    ceiling->crush = true;
    ceiling->type = gen->silent ? silentCrushAndRaise : crushAndRaise;
    ceiling->topheight = sec->ceilingheight;
    ceiling->bottomheight = sec->floorheight + (8*FRACUNIT);
    ceiling->direction = -1;
    ceiling->speed = CEILSPEED << gen->speed_shift;
    ceiling->basespeed = ceiling->speed;
    ceiling->tag = sec->tag;
    P_AddActiveCeiling(ceiling);
    return 1;
}

//
// Boom generalized crusher.
//
// Every generalized crusher crushes, so the type only selects silent or audible
// and the speed comes from the bits. basespeed is stored so T_MoveCeiling does
// not reset a turbo crusher to CEILSPEED at the bottom of its stroke.
//
int
EV_DoGenCrusher
( line_t*	line )
{
    int			secnum;
    int			rtn;
    dpt_gencrusher_t	gen;

    if (!DPT_GenCrusherDecode(line->special, &gen)
        || !DPT_GenCrusherSupported(line->special))
        return 0;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
	if (GenCrusherOnSector(&sectors[secnum], &gen))
	    rtn = 1;
    return rtn;
}

//
// Push-trigger entry point for generalized crushers.
//
int
EV_DoGenCrusherSector
( sector_t*	sec,
  int		special )
{
    dpt_gencrusher_t	gen;

    if (!sec
        || !DPT_GenCrusherDecode(special, &gen)
        || !DPT_GenCrusherSupported(special))
        return 0;
    return GenCrusherOnSector(sec, &gen);
}


//
// Add an active ceiling
//
void P_AddActiveCeiling(ceiling_t* c)
{
    int		i;
    
    for (i = 0; i < MAXCEILINGS;i++)
    {
	if (activeceilings[i] == NULL)
	{
	    activeceilings[i] = c;
	    return;
	}
    }
}



//
// Remove a ceiling's thinker
//
void P_RemoveActiveCeiling(ceiling_t* c)
{
    int		i;
	
    for (i = 0;i < MAXCEILINGS;i++)
    {
	if (activeceilings[i] == c)
	{
	    activeceilings[i]->sector->specialdata = NULL;
	    P_RemoveThinker (&activeceilings[i]->thinker);
	    activeceilings[i] = NULL;
	    break;
	}
    }
}



//
// Restart a ceiling that's in-stasis
//
void P_ActivateInStasisCeiling(line_t* line)
{
    int		i;
	
    for (i = 0;i < MAXCEILINGS;i++)
    {
	if (activeceilings[i]
	    && (activeceilings[i]->tag == line->tag)
	    && (activeceilings[i]->direction == 0))
	{
	    activeceilings[i]->direction = activeceilings[i]->olddirection;
	    activeceilings[i]->thinker.function.acp1
	      = (actionf_p1)T_MoveCeiling;
	}
    }
}



//
// EV_CeilingCrushStop
// Stop a ceiling from crushing!
//
int	EV_CeilingCrushStop(line_t	*line)
{
    int		i;
    int		rtn;
	
    rtn = 0;
    for (i = 0;i < MAXCEILINGS;i++)
    {
	if (activeceilings[i]
	    && (activeceilings[i]->tag == line->tag)
	    && (activeceilings[i]->direction != 0))
	{
	    activeceilings[i]->olddirection = activeceilings[i]->direction;
	    activeceilings[i]->thinker.function.acv = (actionf_v)NULL;
	    activeceilings[i]->direction = 0;		// in-stasis
	    rtn = 1;
	}
    }
    

    return rtn;
}
