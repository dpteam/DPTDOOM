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
//      Timer functions.
//

#include "i_timer.h"
#include "doomtype.h"

#include "doomgeneric.h"

#include <stdarg.h>

//#include <sys/time.h>
//#include <unistd.h>


//
// I_GetTime
// returns time in 1/35th second tics
//

static uint32_t basetime = 0;
static boolean timer_started = false;

static uint32_t ElapsedMilliseconds(void)
{
    uint32_t ticks = DG_GetTicksMs();
    if (!timer_started)
    {
        basetime = ticks;
        timer_started = true;
    }
    // Unsigned subtraction also handles the platform's 32-bit clock wrap.
    return ticks - basetime;
}


int I_GetTicks(void)
{
	return DG_GetTicksMs();
}

int  I_GetTime (void)
{
    // Widen before multiplication: the old expression wrapped after ~34h.
    return (int)(((uint64_t)ElapsedMilliseconds() * TICRATE) / 1000);
}


//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    return (int)ElapsedMilliseconds();
}

// Sleep for a specified number of ms

void I_Sleep(int ms)
{
    //SDL_Delay(ms);
    //usleep (ms * 1000);

	DG_SleepMs(ms);
}

void I_WaitVBL(int count)
{
    //I_Sleep((count * 1000) / 70);
}


void I_InitTimer(void)
{
    // initialize timer

    //SDL_Init(SDL_INIT_TIMER);
}

