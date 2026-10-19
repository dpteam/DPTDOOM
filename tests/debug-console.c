// Standalone console relay probe; no IWAD required. GPL-2.0-or-later.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "dpt_debug_console.h"
int main(void)
{
    unsigned int i;
    DPT_DebugConsoleInit();
    printf("Probe: console=%d\n",GetConsoleWindow()!=NULL);
    puts("STDOUT-PROBE"); fprintf(stderr,"STDERR-PROBE\n");
    for(i=0;i<5000;++i)printf("relay-line-%04u-abcdefghijklmnopqrstuvwxyz\n",i);
    puts("END-PROBE");
    return 0;
}
