#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "i_system.h"
#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// This must work without a display or an IWAD (build/launcher probe).
	if (argc == 2 && (!strcmp(argv[1], "--version")
	                 || !strcmp(argv[1], "-version")))
	{
		puts(PACKAGE_STRING);
		exit(0);
	}

	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*DG_ScreenBuffer));
	if (DG_ScreenBuffer == NULL)
	{
		I_Error("Failed to allocate screen buffer");
	}

	DG_Init();

	D_DoomMain ();
}

