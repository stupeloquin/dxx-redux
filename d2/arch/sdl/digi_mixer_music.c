/*
 * This is an alternate backend for the music system.
 * It uses SDL_mixer to provide a more reliable playback,
 * and allow processing of multiple audio formats.
 *
 *  -- MD2211 (2006-04-24)
 */

#include <SDL.h>
#include <SDL_mixer.h>
#include <string.h>
#include <stdlib.h>

#include "args.h"
#include "hmp.h"
#include "digi_mixer_music.h"
#include "u_mem.h"
#include "console.h"

#ifdef __ANDROID__
#include "adlmidi_dynamic.h"
static ADL_MIDIPlayer *adlmidi_player = NULL;
static int adlmidi_playing = 0;

static void mix_adlmidi_callback(void *udata, Uint8 *stream, int len)
{
	ADLMIDI_AudioFormat format;
	int sampleCount;
	(void)udata;

	if (!adlmidi_player || !adlmidi_playing)
	{
		memset(stream, 0, len);
		return;
	}

	format.type = ADLMIDI_SampleType_S16;
	format.containerSize = sizeof(Sint16);
	format.sampleOffset = sizeof(Sint16) * 2; /* stereo interleaved */
	sampleCount = len / (int)sizeof(Sint16);

	adl_playFormat(adlmidi_player, sampleCount, stream, stream + sizeof(Sint16), &format);
}
#endif

#ifdef _WIN32
extern int digi_win32_play_midi_song( char * filename, int loop );
#endif
Mix_Music *current_music = NULL;
static unsigned char *current_music_hndlbuf = NULL;


/*
 *  Plays a music file from an absolute path or a relative path
 */

int mix_play_file(char *filename, int loop, void (*hook_finished_track)())
{
	SDL_RWops *rw = NULL;
	PHYSFS_file *filehandle = NULL;
	char full_path[PATH_MAX];
	char *fptr;
	unsigned int bufsize = 0;

	mix_free_music();	// stop and free what we're already playing, if anything

	fptr = strrchr(filename, '.');

	if (fptr == NULL)
		return 0;

	// It's a .hmp!
	if (!d_stricmp(fptr, ".hmp"))
	{
#ifdef __ANDROID__
		/* Try ADLMIDI first — OPL3 synth with embedded Descent bank */
		if (adlmidi_load())
		{
			if (!adlmidi_player)
			{
				int freq;
				Mix_QuerySpec(&freq, NULL, NULL);
				if (!freq) freq = 44100;
				adlmidi_player = adl_init(freq);
				if (adlmidi_player)
				{
					adl_switchEmulator(adlmidi_player, ADLMIDI_EMU_DOSBOX);
					adl_setNumChips(adlmidi_player, 2);
					adl_setBank(adlmidi_player, ADL_BANK_DESCENT);
					adl_setSoftPanEnabled(adlmidi_player, 1);
				}
			}
			if (adlmidi_player)
			{
				hmp2mid(filename, &current_music_hndlbuf, &bufsize);
				if (current_music_hndlbuf && bufsize > 0 &&
				    adl_openData(adlmidi_player, current_music_hndlbuf, bufsize) == 0)
				{
					adl_setLoopEnabled(adlmidi_player, loop ? 1 : 0);
					adlmidi_playing = 1;
					Mix_HookMusic(mix_adlmidi_callback, NULL);
					Mix_HookMusicFinished(hook_finished_track ? hook_finished_track : mix_free_music);
					return 1;
				}
				con_printf(CON_NORMAL, "ADLMIDI: failed to open MIDI data, falling back");
			}
		}
#endif
		hmp2mid(filename, &current_music_hndlbuf, &bufsize);
		rw = SDL_RWFromConstMem(current_music_hndlbuf,bufsize*sizeof(char));
		current_music = Mix_LoadMUS_RW(rw, 0);
	}

	// try loading music via given filename
	if (!current_music)
		current_music = Mix_LoadMUS(filename);

	// allow the shell convention tilde character to mean the user's home folder
	// chiefly used for default jukebox level song music referenced in 'descent.m3u' for Mac OS X
	if (!current_music && *filename == '~')
	{
		snprintf(full_path, PATH_MAX, "%s%s", PHYSFS_getUserDir(),
				 &filename[1 + (!strncmp(&filename[1], PHYSFS_getDirSeparator(), strlen(PHYSFS_getDirSeparator())) ? 
				 strlen(PHYSFS_getDirSeparator()) : 0)]);
		current_music = Mix_LoadMUS(full_path);
		if (current_music)
			filename = full_path;	// used later for possible error reporting
	}
		

	// no luck. so it might be in Searchpath. So try to build absolute path
	if (!current_music)
	{
		PHYSFSX_getRealPath(filename, full_path);
		current_music = Mix_LoadMUS(full_path);
		if (current_music)
			filename = full_path;	// used later for possible error reporting
	}

	// still nothin'? Let's open via PhysFS in case it's located inside an archive
	if (!current_music)
	{
		filehandle = PHYSFS_openRead(filename);
		if (filehandle != NULL)
		{
			current_music_hndlbuf = d_realloc(current_music_hndlbuf, sizeof(char *)*PHYSFS_fileLength(filehandle));
			bufsize = PHYSFS_read(filehandle, current_music_hndlbuf, sizeof(char), PHYSFS_fileLength(filehandle));
			rw = SDL_RWFromConstMem(current_music_hndlbuf,bufsize*sizeof(char));
			PHYSFS_close(filehandle);
			current_music = Mix_LoadMUS_RW(rw, 0);
		}
	}

	if (current_music)
	{
		Mix_PlayMusic(current_music, (loop ? -1 : 1));
		Mix_HookMusicFinished(hook_finished_track ? hook_finished_track : mix_free_music);
		return 1;
	}
	else
	{
		con_printf(CON_CRITICAL,"Music %s could not be loaded: %s\n", filename, Mix_GetError());
		mix_stop_music();
	}

	return 0;
}

// What to do when stopping song playback
void mix_free_music()
{
#ifdef __ANDROID__
	if (adlmidi_playing)
	{
		Mix_HookMusic(NULL, NULL);
		adlmidi_playing = 0;
	}
#endif
	Mix_HaltMusic();
	if (current_music)
	{
		Mix_FreeMusic(current_music);
		current_music = NULL;
	}
	if (current_music_hndlbuf)
	{
		d_free(current_music_hndlbuf);
		current_music_hndlbuf = NULL;
	}
}

void mix_set_music_volume(int vol)
{
	vol *= MIX_MAX_VOLUME/8;
	Mix_VolumeMusic(vol);
}

void mix_stop_music()
{
#ifdef __ANDROID__
	if (adlmidi_playing)
	{
		Mix_HookMusic(NULL, NULL);
		adlmidi_playing = 0;
	}
#endif
	Mix_HaltMusic();
	if (current_music_hndlbuf)
	{
		d_free(current_music_hndlbuf);
		current_music_hndlbuf = NULL;
	}
}

void mix_pause_music()
{
	Mix_PauseMusic();
}

void mix_resume_music()
{
	Mix_ResumeMusic();
}

void mix_pause_resume_music()
{
	if (Mix_PausedMusic())
		Mix_ResumeMusic();
	else if (Mix_PlayingMusic())
		Mix_PauseMusic();
}
