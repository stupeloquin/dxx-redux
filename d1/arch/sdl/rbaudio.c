/*
 *
 * SDL2 CD Audio stub functions
 * SDL2 removed the CDROM API; these are no-op stubs.
 *
 */

#include "pstypes.h"
#include "rbaudio.h"
#include "console.h"

void (*redbook_finished_hook)() = NULL;

void RBAInit()
{
}

void RBAExit()
{
}

int RBAEnabled()
{
	return 0;
}

int RBAPlayTrack(int a)
{
	return -1;
}

void RBAStop()
{
}

void RBAEjectDisk()
{
}

void RBASetVolume(int volume)
{
}

void RBAPause()
{
}

int RBAResume()
{
	return -1;
}

int RBAPauseResume()
{
	return 0;
}

int RBAGetNumberOfTracks()
{
	return -1;
}

void RBACheckFinishedHook()
{
}

int RBAPlayTracks(int first, int last, void (*hook_finished)(void))
{
	return 0;
}

int RBAGetTrackNum()
{
	return 0;
}

int RBAPeekPlayStatus()
{
	return 0;
}

unsigned long RBAGetDiscID()
{
	return 0;
}

void RBAList(void)
{
}
