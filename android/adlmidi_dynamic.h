/*
 * Dynamic interface to libADLMIDI, the OPL3 synthesizer library.
 * C adaptation for dxx-redux from dxx-rebirth-android.
 */

#ifndef _ADLMIDI_DYNAMIC_H
#define _ADLMIDI_DYNAMIC_H

#include <stdint.h>

typedef struct ADL_MIDIPlayer ADL_MIDIPlayer;

enum ADLMIDI_SampleType
{
	ADLMIDI_SampleType_S16 = 0
};

typedef struct ADLMIDI_AudioFormat
{
	enum ADLMIDI_SampleType type;
	unsigned containerSize;
	unsigned sampleOffset;
} ADLMIDI_AudioFormat;

enum ADL_Emulator
{
	ADLMIDI_EMU_DOSBOX = 2
};

enum ADL_EmbeddedBank
{
	ADL_BANK_MILES_AIL = 0,
	ADL_BANK_BISQWIT = 1,
	ADL_BANK_DESCENT = 2,
	ADL_BANK_THE_FATMAN_2OP = 58,
	ADL_BANK_THE_FATMAN_4OP = 59
};

extern ADL_MIDIPlayer *(*adl_init)(long sample_rate);
extern void (*adl_close)(ADL_MIDIPlayer *device);
extern int (*adl_switchEmulator)(ADL_MIDIPlayer *device, int emulator);
extern int (*adl_setNumChips)(ADL_MIDIPlayer *device, int numChips);
extern int (*adl_setBank)(ADL_MIDIPlayer *device, int bank);
extern void (*adl_setSoftPanEnabled)(ADL_MIDIPlayer *device, int softPanEn);
extern void (*adl_setLoopEnabled)(ADL_MIDIPlayer *device, int loopEn);
extern int (*adl_openData)(ADL_MIDIPlayer *device, const void *mem, unsigned long size);
extern int (*adl_openFile)(ADL_MIDIPlayer *device, const char *filePath);
extern int (*adl_playFormat)(ADL_MIDIPlayer *device, int sampleCount, uint8_t *left, uint8_t *right, const ADLMIDI_AudioFormat *format);

/* Call once to attempt loading libADLMIDI.so. Returns 1 on success, 0 on failure. */
int adlmidi_load(void);

#endif /* _ADLMIDI_DYNAMIC_H */
