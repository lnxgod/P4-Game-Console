#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;
static int doomgeneric_quit_requested = 0;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	doomgeneric_quit_requested = 0;
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

	DG_Init();

	D_DoomMain ();
}

void doomgeneric_RequestQuit(void)
{
	doomgeneric_quit_requested = 1;
}

int doomgeneric_QuitRequested(void)
{
	return doomgeneric_quit_requested;
}
