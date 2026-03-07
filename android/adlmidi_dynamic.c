/*
 * Dynamic interface to libADLMIDI, the OPL3 synthesizer library.
 * C adaptation for dxx-redux from dxx-rebirth-android.
 */

#include "adlmidi_dynamic.h"
#include "console.h"
#include <dlfcn.h>
#ifdef __ANDROID__
#include <SDL.h>
#endif

static ADL_MIDIPlayer *adl_init_failure(long sample_rate)
{
	(void)sample_rate;
	return NULL;
}

ADL_MIDIPlayer *(*adl_init)(long sample_rate) = adl_init_failure;
void (*adl_close)(ADL_MIDIPlayer *device) = NULL;
int (*adl_switchEmulator)(ADL_MIDIPlayer *device, int emulator) = NULL;
int (*adl_setNumChips)(ADL_MIDIPlayer *device, int numChips) = NULL;
int (*adl_setBank)(ADL_MIDIPlayer *device, int bank) = NULL;
void (*adl_setSoftPanEnabled)(ADL_MIDIPlayer *device, int softPanEn) = NULL;
void (*adl_setLoopEnabled)(ADL_MIDIPlayer *device, int loopEn) = NULL;
int (*adl_openData)(ADL_MIDIPlayer *device, const void *mem, unsigned long size) = NULL;
int (*adl_openFile)(ADL_MIDIPlayer *device, const char *filePath) = NULL;
int (*adl_playFormat)(ADL_MIDIPlayer *device, int sampleCount, uint8_t *left, uint8_t *right, const ADLMIDI_AudioFormat *format) = NULL;

static int load_sym(void *handle, const char *name, void **fptr)
{
	void *f = dlsym(handle, name);
	*fptr = f;
	if (!f)
	{
		con_printf(CON_NORMAL, "ADLMIDI: failed to load \"%s\"", name);
		return 0;
	}
	return 1;
}

int adlmidi_load(void)
{
	void *handle;

	/* Already loaded successfully */
	if (adl_init != adl_init_failure)
		return 1;

	handle = dlopen("libADLMIDI.so", RTLD_NOW);
	if (!handle)
	{
		con_printf(CON_NORMAL, "ADLMIDI: failed to load libADLMIDI.so: %s", dlerror());
#ifdef __ANDROID__
		SDL_Log("ADLMIDI: failed to load libADLMIDI.so: %s", dlerror());
#endif
		return 0;
	}

	if (!load_sym(handle, "adl_init", (void **)&adl_init) ||
	    !load_sym(handle, "adl_close", (void **)&adl_close) ||
	    !load_sym(handle, "adl_switchEmulator", (void **)&adl_switchEmulator) ||
	    !load_sym(handle, "adl_setNumChips", (void **)&adl_setNumChips) ||
	    !load_sym(handle, "adl_setBank", (void **)&adl_setBank) ||
	    !load_sym(handle, "adl_setSoftPanEnabled", (void **)&adl_setSoftPanEnabled) ||
	    !load_sym(handle, "adl_setLoopEnabled", (void **)&adl_setLoopEnabled) ||
	    !load_sym(handle, "adl_openData", (void **)&adl_openData) ||
	    !load_sym(handle, "adl_openFile", (void **)&adl_openFile) ||
	    !load_sym(handle, "adl_playFormat", (void **)&adl_playFormat))
	{
		adl_init = adl_init_failure;
		dlclose(handle);
		return 0;
	}

	con_printf(CON_NORMAL, "ADLMIDI: loaded OPL3 synthesizer");
#ifdef __ANDROID__
	SDL_Log("ADLMIDI: loaded OPL3 synthesizer");
#endif
	return 1;
}
