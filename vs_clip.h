/*	Public domain	*/

#ifndef _VISLAK_CLIP_H_
#define _VISLAK_CLIP_H_

#include <sndfile.h>
#include <portaudio2/portaudio.h>

#include "vs_midi.h"

typedef struct vs_frame {
	AG_Surface *thumb;		/* Generated thumbnail */
	Uint f;				/* Original frame# */
	Uint flags;
#define VS_FRAME_SELECTED	0x01	/* Frame is selected */
	int midiKey;			/* Assigned MIDI key */
	int kbdKey;			/* Assigned keyboard key */
} VS_Frame;

typedef struct vs_clip {
	AG_Mutex lock;			/* Lock on video data */
	VS_Frame *frames;		/* Frames in memory */
	Uint n;				/* Total number of frames */
	char *dir;			/* Directory containing video frames */
	char *fileFmt;			/* Format string for frame files */

	AG_Mutex   sndLock;		/* Lock on audio data */
	SNDFILE   *sndFile;		/* Associated audio clip */
	SF_INFO    sndInfo;
	sf_count_t sndPos;		/* Position in audio stream */
	float     *sndBuf;		/* Audio buffer */
	float     *sndViz;		/* Reduced visualization buffer */
	sf_count_t sndVizFrames;
	double     sndPeakSignal;	/* Signal peak in audio stream */
	PaStream  *sndStream;		/* For PortAudio playback */
	int        samplesPerFrame;	/* Audio samples per video frame */
	VS_Midi    *midi;		/* MIDI settings */
	int         kbdKeymap[AG_KEY_LAST]; /* Keyboard frame mappings */
} VS_Clip;

__BEGIN_DECLS
VS_Clip *VS_ClipNew(void);
void     VS_ClipDestroy(VS_Clip *);
void     VS_ClipSetArchivePath(void *, const char *);
int      VS_ClipAddFrame(VS_Clip *, const char *);
void     VS_ClipDelFrames(VS_Clip *, Uint, Uint);
int      VS_ClipCopyFrame(VS_Clip *, VS_Clip *, Uint);
void     VS_ClipGetFramePath(VS_Clip *, Uint, char *, size_t);
Uint     VS_ClipClearKeys(VS_Clip *);
__END_DECLS

#endif /* _VISLAK_CLIP_H_ */
