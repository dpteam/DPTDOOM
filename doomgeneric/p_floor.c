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
// DESCRIPTION:
//	Floor animation: raising stairs.
//



#include "z_zone.h"
#include "doomdef.h"
#include "dpt_genfloor.h"
#include "dpt_genstairs.h"
#include "p_local.h"

#include "s_sound.h"

// State.
#include "doomstat.h"
#include "r_state.h"
// Data.
#include "sounds.h"


//
// FLOORS
//

//
// Move a plane (floor or ceiling) and check for crushing
//
result_e
T_MovePlane
( sector_t*	sector,
  fixed_t	speed,
  fixed_t	dest,
  boolean	crush,
  int		floorOrCeiling,
  int		direction )
{
    boolean	flag;
    fixed_t	lastpos;
	
    switch(floorOrCeiling)
    {
      case 0:
	// FLOOR
	switch(direction)
	{
	  case -1:
	    // DOWN
	    if (sector->floorheight - speed < dest)
	    {
		lastpos = sector->floorheight;
		sector->floorheight = dest;
		flag = P_ChangeSector(sector,crush);
		if (flag == true)
		{
		    sector->floorheight =lastpos;
		    P_ChangeSector(sector,crush);
		    //return crushed;
		}
		return pastdest;
	    }
	    else
	    {
		lastpos = sector->floorheight;
		sector->floorheight -= speed;
		flag = P_ChangeSector(sector,crush);
		if (flag == true)
		{
		    sector->floorheight = lastpos;
		    P_ChangeSector(sector,crush);
		    return crushed;
		}
	    }
	    break;
						
	  case 1:
	    // UP
	    if (sector->floorheight + speed > dest)
	    {
		lastpos = sector->floorheight;
		sector->floorheight = dest;
		flag = P_ChangeSector(sector,crush);
		if (flag == true)
		{
		    sector->floorheight = lastpos;
		    P_ChangeSector(sector,crush);
		    //return crushed;
		}
		return pastdest;
	    }
	    else
	    {
		// COULD GET CRUSHED
		lastpos = sector->floorheight;
		sector->floorheight += speed;
		flag = P_ChangeSector(sector,crush);
		if (flag == true)
		{
		    if (crush == true)
			return crushed;
		    sector->floorheight = lastpos;
		    P_ChangeSector(sector,crush);
		    return crushed;
		}
	    }
	    break;
	}
	break;
									
      case 1:
	// CEILING
	switch(direction)
	{
	  case -1:
	    // DOWN
	    if (sector->ceilingheight - speed < dest)
	    {
		lastpos = sector->ceilingheight;
		sector->ceilingheight = dest;
		flag = P_ChangeSector(sector,crush);

		if (flag == true)
		{
		    sector->ceilingheight = lastpos;
		    P_ChangeSector(sector,crush);
		    //return crushed;
		}
		return pastdest;
	    }
	    else
	    {
		// COULD GET CRUSHED
		lastpos = sector->ceilingheight;
		sector->ceilingheight -= speed;
		flag = P_ChangeSector(sector,crush);

		if (flag == true)
		{
		    if (crush == true)
			return crushed;
		    sector->ceilingheight = lastpos;
		    P_ChangeSector(sector,crush);
		    return crushed;
		}
	    }
	    break;
						
	  case 1:
	    // UP
	    if (sector->ceilingheight + speed > dest)
	    {
		lastpos = sector->ceilingheight;
		sector->ceilingheight = dest;
		flag = P_ChangeSector(sector,crush);
		if (flag == true)
		{
		    sector->ceilingheight = lastpos;
		    P_ChangeSector(sector,crush);
		    //return crushed;
		}
		return pastdest;
	    }
	    else
	    {
		lastpos = sector->ceilingheight;
		sector->ceilingheight += speed;
		flag = P_ChangeSector(sector,crush);
// UNUSED
#if 0
		if (flag == true)
		{
		    sector->ceilingheight = lastpos;
		    P_ChangeSector(sector,crush);
		    return crushed;
		}
#endif
	    }
	    break;
	}
	break;
		
    }
    return ok;
}


//
// MOVE A FLOOR TO IT'S DESTINATION (UP OR DOWN)
//
void T_MoveFloor(floormove_t* floor)
{
    result_e	res;
	
    res = T_MovePlane(floor->sector,
		      floor->speed,
		      floor->floordestheight,
		      floor->crush,0,floor->direction);
    
    if (!(leveltime&7))
	S_StartSound(&floor->sector->soundorg, sfx_stnmov);
    
    if (res == pastdest)
    {
	floor->sector->specialdata = NULL;

	if (floor->direction == 1)
	{
	    switch(floor->type)
	    {
	      case donutRaise:
		floor->sector->special = floor->newspecial;
		floor->sector->floorpic = floor->texture;
	      default:
		break;
	    }
	}
	else if (floor->direction == -1)
	{
	    switch(floor->type)
	    {
	      case lowerAndChange:
		floor->sector->special = floor->newspecial;
		floor->sector->floorpic = floor->texture;
	      default:
		break;
	    }
	}
	P_RemoveThinker(&floor->thinker);

	S_StartSound(&floor->sector->soundorg, sfx_pstop);
    }

}

//
// HANDLE FLOOR TYPES
//
int
EV_DoFloor
( line_t*	line,
  floor_e	floortype )
{
    int			secnum;
    int			rtn;
    int			i;
    sector_t*		sec;
    floormove_t*	floor;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	sec = &sectors[secnum];
		
	// ALREADY MOVING?  IF SO, KEEP GOING...
	if (sec->specialdata)
	    continue;
	
	// new floor thinker
	rtn = 1;
	floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	memset(floor, 0, sizeof(*floor));
	P_AddThinker (&floor->thinker);
	sec->specialdata = floor;
	floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	floor->type = floortype;
	floor->crush = false;

	switch(floortype)
	{
	  case lowerFloor:
	    floor->direction = -1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = 
		P_FindHighestFloorSurrounding(sec);
	    break;

	  case lowerFloorToLowest:
	    floor->direction = -1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = 
		P_FindLowestFloorSurrounding(sec);
	    break;

	  case turboLower:
	    floor->direction = -1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED * 4;
	    floor->floordestheight = 
		P_FindHighestFloorSurrounding(sec);
	    if (floor->floordestheight != sec->floorheight)
		floor->floordestheight += 8*FRACUNIT;
	    break;

	  case raiseFloorCrush:
	    floor->crush = true;
	  case raiseFloor:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = 
		P_FindLowestCeilingSurrounding(sec);
	    if (floor->floordestheight > sec->ceilingheight)
		floor->floordestheight = sec->ceilingheight;
	    floor->floordestheight -= (8*FRACUNIT)*
		(floortype == raiseFloorCrush);
	    break;

	  case raiseFloorTurbo:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED*4;
	    floor->floordestheight = 
		P_FindNextHighestFloor(sec,sec->floorheight);
	    break;

	  case raiseFloorToNearest:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = 
		P_FindNextHighestFloor(sec,sec->floorheight);
	    break;

	  case raiseFloor24:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = floor->sector->floorheight +
		24 * FRACUNIT;
	    break;
	  case raiseFloor512:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = floor->sector->floorheight +
		512 * FRACUNIT;
	    break;

	  case raiseFloor24AndChange:
	    floor->direction = 1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = floor->sector->floorheight +
		24 * FRACUNIT;
	    sec->floorpic = line->frontsector->floorpic;
	    sec->special = line->frontsector->special;
	    break;

	  case raiseToTexture:
	  {
	      int	minsize = INT_MAX;
	      side_t*	side;
				
	      floor->direction = 1;
	      floor->sector = sec;
	      floor->speed = FLOORSPEED;
	      for (i = 0; i < sec->linecount; i++)
	      {
		  if (twoSided (secnum, i) )
		  {
		      side = getSide(secnum,i,0);
		      if (side->bottomtexture >= 0)
			  if (textureheight[side->bottomtexture] < 
			      minsize)
			      minsize = 
				  textureheight[side->bottomtexture];
		      side = getSide(secnum,i,1);
		      if (side->bottomtexture >= 0)
			  if (textureheight[side->bottomtexture] < 
			      minsize)
			      minsize = 
				  textureheight[side->bottomtexture];
		  }
	      }
	      floor->floordestheight =
		  floor->sector->floorheight + minsize;
	  }
	  break;
	  
	  case lowerAndChange:
	    floor->direction = -1;
	    floor->sector = sec;
	    floor->speed = FLOORSPEED;
	    floor->floordestheight = 
		P_FindLowestFloorSurrounding(sec);
	    floor->texture = sec->floorpic;

	    for (i = 0; i < sec->linecount; i++)
	    {
		if ( twoSided(secnum, i) )
		{
		    if (getSide(secnum,i,0)->sector-sectors == secnum)
		    {
			sec = getSector(secnum,i,1);

			if (sec->floorheight == floor->floordestheight)
			{
			    floor->texture = sec->floorpic;
			    floor->newspecial = sec->special;
			    break;
			}
		    }
		    else
		    {
			sec = getSector(secnum,i,0);

			if (sec->floorheight == floor->floordestheight)
			{
			    floor->texture = sec->floorpic;
			    floor->newspecial = sec->special;
			    break;
			}
		    }
		}
	    }
	  default:
	    break;
	}
    }
    return rtn;
}


//
// Start one generalized floor on a single sector. Returns 1 when a thinker was
// created. Shared by the tag loop and the push-trigger path.
//
static int
GenFloorOnSector
( sector_t*		sec,
  const dpt_genfloor_t*	gen )
{
    floormove_t*	floor;

    // ALREADY MOVING?  IF SO, KEEP GOING...
    if (sec->specialdata)
	return 0;

    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
    memset(floor, 0, sizeof(*floor));
    P_AddThinker (&floor->thinker);
    sec->specialdata = floor;
    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
    floor->sector = sec;
    floor->speed = FLOORSPEED << gen->speed_shift;
    floor->crush = false;

    // Destination comes from the tested pure function; the engine only gathers
    // the neighbour heights it needs.
    {
	dpt_floor_env_t env;
	env.current = sec->floorheight;
	env.highest_floor = P_FindHighestFloorSurrounding(sec);
	env.lowest_floor = P_FindLowestFloorSurrounding(sec);
	env.next_lowest_floor = P_FindNextLowestFloor(sec, sec->floorheight);
	env.next_highest_floor = P_FindNextHighestFloor(sec, sec->floorheight);
	env.lowest_ceiling = P_FindLowestCeilingSurrounding(sec);
	env.ceiling = sec->ceilingheight;
	floor->floordestheight = DPT_GenFloorDest(gen, &env);
    }

    if (!gen->up)
    {
	floor->direction = -1;
	// Every generalized floor is an explicit destination, so the plain
	// lowering type is enough; the destination is what differs.
	floor->type = lowerFloor;
    }
    else if (gen->target == DPT_FLOOR_TARGET_NNF)
    {
	floor->direction = 1;
	floor->type = raiseFloorToNearest;
    }
    else
    {
	floor->direction = 1;
	floor->type = gen->crush ? raiseFloorCrush : raiseFloor;
	floor->crush = gen->crush;
    }
    return 1;
}

//
// Boom generalized floor.
//
// Only the subset that maps exactly onto an existing floor behaviour is
// accepted (see DPT_GenFloorSupported). Speed comes from the generalized bits
// instead of the fixed FLOORSPEED used by the vanilla types, so the thinker is
// built here rather than reusing EV_DoFloor.
//
int
EV_DoGenFloor
( line_t*	line )
{
    int			secnum;
    int			rtn;
    dpt_genfloor_t	gen;

    if (!DPT_GenFloorDecode(line->special, &gen)
        || !DPT_GenFloorSupported(line->special))
        return 0;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
	if (GenFloorOnSector(&sectors[secnum], &gen))
	    rtn = 1;
    return rtn;
}

//
// Push-trigger entry point: BOOM treats push as a switch with a zero tag, so the
// generalized floor applies to the sector behind the line instead of by tag.
//
int
EV_DoGenFloorSector
( sector_t*	sec,
  int		special )
{
    dpt_genfloor_t	gen;

    if (!sec
        || !DPT_GenFloorDecode(special, &gen)
        || !DPT_GenFloorSupported(special))
        return 0;
    return GenFloorOnSector(sec, &gen);
}




//
// Boom generalized stair builder.
//
// Mirrors EV_BuildStairs' traversal (follow the same-front-sided two-sided lines
// from the tagged sector) but with the step size, speed, direction and the
// texture rule taken from the generalized bits. The vanilla builder only goes up
// with a fixed 8/16 step, so it cannot be reused directly.
//
static int
GenStairsFromSector
( sector_t*		sec,
  const dpt_genstairs_t*	gen )
{
    int			secnum;
    int			i;
    int			newsecnum;
    int			texture;
    int			ok;
    sector_t*		tsec;
    floormove_t*	floor;

    fixed_t		stairsize;
    fixed_t		speed;
    fixed_t		height;

    // ALREADY MOVING?  IF SO, KEEP GOING...
    if (sec->specialdata)
	return 0;

    stairsize = gen->step * FRACUNIT;
    speed = (FRACUNIT / 8) * gen->speed_eighths;
    secnum = sec - sectors;

    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
    memset(floor, 0, sizeof(*floor));
    P_AddThinker (&floor->thinker);
    sec->specialdata = floor;
    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
    floor->direction = gen->up ? 1 : -1;
    floor->sector = sec;
    floor->speed = speed;
    height = gen->up ? sec->floorheight + stairsize
		     : sec->floorheight - stairsize;
    floor->floordestheight = height;

    texture = sec->floorpic;

    // Find next sector to raise (or lower):
    // 1.	Find 2-sided line with same sector side[0]
    // 2.	Other side is the next sector to move
    do
    {
	ok = 0;
	for (i = 0;i < sec->linecount;i++)
	{
	    if ( !((sec->lines[i])->flags & ML_TWOSIDED) )
		continue;

	    tsec = (sec->lines[i])->frontsector;
	    newsecnum = tsec-sectors;

	    if (secnum != newsecnum)
		continue;

	    tsec = (sec->lines[i])->backsector;
	    newsecnum = tsec - sectors;

	    if (!gen->ignore_texture && tsec->floorpic != texture)
		continue;

	    height += gen->up ? stairsize : -stairsize;

	    if (tsec->specialdata)
		continue;

	    sec = tsec;
	    secnum = newsecnum;
	    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	    memset(floor, 0, sizeof(*floor));

	    P_AddThinker (&floor->thinker);

	    sec->specialdata = floor;
	    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	    floor->direction = gen->up ? 1 : -1;
	    floor->sector = sec;
	    floor->speed = speed;
	    floor->floordestheight = height;
	    ok = 1;
	    break;
	}
    } while(ok);
    return 1;
}

int
EV_DoGenStairs
( line_t*	line )
{
    int			secnum;
    int			rtn;
    dpt_genstairs_t	gen;

    if (!DPT_GenStairsDecode(line->special, &gen)
        || !DPT_GenStairsSupported(line->special))
        return 0;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
	if (GenStairsFromSector(&sectors[secnum], &gen))
	    rtn = 1;
    return rtn;
}

//
// Push-trigger entry point: the stair chain starts at the sector behind the
// line. BOOM has no tag for push, so the chain simply begins there.
//
int
EV_DoGenStairsSector
( sector_t*	sec,
  int		special )
{
    dpt_genstairs_t	gen;

    if (!sec
        || !DPT_GenStairsDecode(special, &gen)
        || !DPT_GenStairsSupported(special))
        return 0;
    return GenStairsFromSector(sec, &gen);
}



//
// BUILD A STAIRCASE!
//
int
EV_BuildStairs
( line_t*	line,
  stair_e	type )
{
    int			secnum;
    int			height;
    int			i;
    int			newsecnum;
    int			texture;
    int			ok;
    int			rtn;
    
    sector_t*		sec;
    sector_t*		tsec;

    floormove_t*	floor;
    
    fixed_t		stairsize = 0;
    fixed_t		speed = 0;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	sec = &sectors[secnum];
		
	// ALREADY MOVING?  IF SO, KEEP GOING...
	if (sec->specialdata)
	    continue;
	
	// new floor thinker
	rtn = 1;
	floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	memset(floor, 0, sizeof(*floor));
	P_AddThinker (&floor->thinker);
	sec->specialdata = floor;
	floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	floor->direction = 1;
	floor->sector = sec;
	switch(type)
	{
	  case build8:
	    speed = FLOORSPEED/4;
	    stairsize = 8*FRACUNIT;
	    break;
	  case turbo16:
	    speed = FLOORSPEED*4;
	    stairsize = 16*FRACUNIT;
	    break;
	}
	floor->speed = speed;
	height = sec->floorheight + stairsize;
	floor->floordestheight = height;
		
	texture = sec->floorpic;
	
	// Find next sector to raise
	// 1.	Find 2-sided line with same sector side[0]
	// 2.	Other side is the next sector to raise
	do
	{
	    ok = 0;
	    for (i = 0;i < sec->linecount;i++)
	    {
		if ( !((sec->lines[i])->flags & ML_TWOSIDED) )
		    continue;
					
		tsec = (sec->lines[i])->frontsector;
		newsecnum = tsec-sectors;
		
		if (secnum != newsecnum)
		    continue;

		tsec = (sec->lines[i])->backsector;
		newsecnum = tsec - sectors;

		if (tsec->floorpic != texture)
		    continue;
					
		height += stairsize;

		if (tsec->specialdata)
		    continue;
					
		sec = tsec;
		secnum = newsecnum;
		floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
		memset(floor, 0, sizeof(*floor));

		P_AddThinker (&floor->thinker);

		sec->specialdata = floor;
		floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
		floor->direction = 1;
		floor->sector = sec;
		floor->speed = speed;
		floor->floordestheight = height;
		ok = 1;
		break;
	    }
	} while(ok);
    }
    return rtn;
}

