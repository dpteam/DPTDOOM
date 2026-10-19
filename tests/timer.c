// Synthetic clock checks; no real waiting or game data.
#include "doomgeneric.h"
#include "i_timer.h"
#include <stdio.h>
#include <string.h>
static uint32_t now;
uint32_t DG_GetTicksMs(void) { return now; }
void DG_SleepMs(uint32_t ms) { now += ms; }

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "wrap"))
    {
        now = UINT32_MAX - 500;
        if (I_GetTime() != 0) return 5;
        now = 499;
        if (I_GetTimeMS() != 1000 || I_GetTime() != 35) return 6;
        puts("Timer: platform counter wrap across nonzero epoch OK");
        return 0;
    }
    // A clock that starts at zero must not keep resetting the baseline.
    if (I_GetTime() != 0) return 1;
    now = 1000;
    if (I_GetTime() != 35 || I_GetTimeMS() != 1000) return 2;
    now = 200000000;
    if (I_GetTime() != 7000000) return 3;
    now = UINT32_MAX - 10;
    if (I_GetTime() != (int)(((uint64_t)now * 35) / 1000)) return 4;
    puts("Timer: zero epoch and wide tic conversion OK (32-bit clock horizon retained)");
    return 0;
}
