/*
 * Copyright (c) 2010-2013 Julien Nadeau (vedge@hypertriton.com).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ViSlak MIDI interface
 */

#include <vislak.h>
#include "icons.h"

#include <config/have_alsa.h>
#ifdef HAVE_ALSA
# include <alsa/asoundlib.h>
#endif

typedef struct vs_midi_pvt {
#ifdef HAVE_ALSA
	snd_rawmidi_t *in;
	snd_rawmidi_t *out;
#else
	int in;
	int out;
#endif
} VS_MidiPvt;

static AG_Thread thInput, thOutput;

VS_Midi *
VS_MidiNew(VS_View *vv)
{
	VS_Midi *mid;
	Uint i;

	mid = Malloc(sizeof(VS_Midi));
	mid->vv = vv;
	mid->pvt = Malloc(sizeof(VS_MidiPvt));
#ifdef HAVE_ALSA
	mid->pvt->in = NULL;
	mid->pvt->out = NULL;
#else
	mid->pvt->in = -1;
	mid->pvt->out = -1;
#endif
	for (i = 0; i < VS_MIDI_MAXKEYS; i++) {
		mid->keymap[i] = -1;
	}
	return (mid);
}

void
VS_MidiDestroy(VS_Midi *mid)
{
#ifdef HAVE_ALSA
	if (mid->pvt->in != NULL) {
		snd_rawmidi_drain(mid->pvt->in);
		snd_rawmidi_close(mid->pvt->in);
	}
	if (mid->pvt->out != NULL) {
		snd_rawmidi_drain(mid->pvt->out);
		snd_rawmidi_close(mid->pvt->out);
	}
#endif
	Free(mid->pvt);
	Free(mid);
}

/* Create a MIDI key->frame mapping */
void
VS_MidiAddKey(VS_Midi *mid, int key, VS_Frame *vf)
{
	mid->keymap[key] = vf->f;
	vf->midiKey = key;
}

/* Remove a MIDI key->frame mapping */
void
VS_MidiDelKey(VS_Midi *mid, int key)
{
	mid->keymap[key] = -1;
}

/* Clear MIDI keymap */
Uint
VS_MidiClearKeys(VS_Midi *mid)
{
	Uint i, nCleared = 0, f;
	VS_Clip *v = mid->vv->clip;

	for (i = 0; i < VS_MIDI_MAXKEYS; i++) {
		if ((f = mid->keymap[i]) != -1) {
			mid->keymap[i] = -1;
			nCleared++;
		}
	}
	for (i = 0; i < v->n; i++) {
		v->frames[i].midiKey = -1;
	}
	return (nCleared);
}

static void
RepartitionMIDI(VS_View *vv, int start, int end)
{
	VS_Clip *v = vv->clip;
	VS_Midi *mid = v->midi;
	Uint i, j, key;
	Uint div, nMapped = 0;

	div = (Uint)(end - start)/60;
	key = 36;
	for (i = start, j = 0;
	     i < end;
	     i++) {
		v->frames[i].midiKey = key;
		if (++j > div) {
			j = 0;
			key++;
		}
		mid->keymap[key] = i;
		nMapped++;
	}
	VS_Status(vv, _("Mapped %u MIDI keys (%d-%d)"),
	    nMapped, start, end);
}

#ifdef HAVE_ALSA
/*
 * MIDI input loop.
 */
static __inline__ void
ReadMidi(VS_View *vv, VS_Midi *mid, void *p, size_t len)
{
	AG_ObjectUnlock(vv->clip->proj);
	AG_ObjectUnlock(vv);
	snd_rawmidi_read(mid->pvt->in, p, len);
	AG_ObjectLock(vv);
	AG_ObjectLock(vv->clip->proj);
}
static void *
VS_MidiInputThread(void *arg)
{
	VS_Midi *mid = arg;
	VS_View *vv = mid->vv;
	VS_Clip *v;
	VS_Project *vsp;
	Uchar ch, data[2];
	int key, vel;
	double bend = 0.0;
	int repStart = 0, repSize = 0;

	AG_ObjectLock(vv);
	v = vv->clip;
	vsp = v->proj;
	AG_ObjectLock(vsp);

	for (;;) {
		ReadMidi(vv, mid, &ch, 1);
		switch (ch) {
		case 0x94:					/* Note */
			ReadMidi(vv, mid, data, 2);
			key = (int)data[0];
			vel = (int)data[1];
			if (vel == 0) {
				break;
			}
			if (vsp->flags & VS_PROJECT_LEARNING &&
			    vv->xSel >= 0 && vv->xSel < v->n) {
				VS_MidiAddKey(mid, key, &v->frames[vv->xSel]);
			} else {
				if (mid->keymap[key] != -1) {
					v->x = mid->keymap[key];
					vv->xSel = mid->keymap[key];
				}
			}
			break;
		case 0xb4:					/* Controller */
			ReadMidi(vv, mid, data, 2);
			switch (data[0]) {
			case 0x1:
				vsp->bendSpeed = 1.0 +
				    ((double)(127 - data[1])/127.0)*vsp->bendSpeedMax;
				v->xVel = bend/vsp->bendSpeed;
				break;
			case 0xa:
				repStart = data[1]*(v->n - 1)/127;
				RepartitionMIDI(vv, repStart, repSize);
				break;
			case 0x1c:
				repSize = data[1]*(v->n - 1)/127;
				RepartitionMIDI(vv, repStart, repSize);
				break;
			default:
				v->x = data[1]*(v->n - 1)/127;
				break;
			}
			break;
		case 0xe4:					/* Pitch bend */
			ReadMidi(vv, mid, data, 2);
			bend = (double)(data[1] - 64);
			v->xVel = bend/vsp->bendSpeed;
			break;
		}
	}
	
	AG_ObjectUnlock(vsp);
	AG_ObjectUnlock(vv);
}

/*
 * MIDI output loop.
 */
static void *
VS_MidiOutputThread(void *arg)
{
	VS_Midi *mid = arg;

	for (;;) {
		Uchar ch;

		ch = 0x90;	snd_rawmidi_write(mid->pvt->out, &ch, 1);
		ch = 60;	snd_rawmidi_write(mid->pvt->out, &ch, 1);
		ch = 100;	snd_rawmidi_write(mid->pvt->out, &ch, 1);
		snd_rawmidi_drain(mid->pvt->out);
		sleep(1);
		ch = 0x90;	snd_rawmidi_write(mid->pvt->out, &ch, 1);
		ch = 60;	snd_rawmidi_write(mid->pvt->out, &ch, 1);
		ch = 0;		snd_rawmidi_write(mid->pvt->out, &ch, 1);
		snd_rawmidi_drain(mid->pvt->out);
		sleep(1);
	}
}

static void
GetDeviceName(char *buf, size_t buf_len, int card, int dev, int subdev)
{
	if (subdev != -1) {
		snprintf(buf, buf_len, "hw:%d,%d,%d", card, dev, subdev);
	} else {
		snprintf(buf, buf_len, "hw:%d,%d", card, dev);
	}
}

static void
SetInputALSA(AG_Event *event)
{
	char devname[16];
	VS_Midi *mid = AG_PTR(1);
	int card = AG_INT(2);
	int dev = AG_INT(3);
	int subdev = AG_INT(4);
	int rv;

	if (mid->pvt->in != NULL) {
		AG_TextError(_("MIDI input device is already open"));
		return;
	}
	GetDeviceName(devname, sizeof(devname), card, dev, subdev);
 	if ((rv = snd_rawmidi_open(&mid->pvt->in, NULL, devname, 0)) != 0) {
		AG_TextError(_("ALSA: Failed to open %s: %s"),
		    devname, snd_strerror(rv));
		return;
	}
	mid->flags |= VS_MIDI_INPUT;
/*	AG_LabelText(vsStatus, _("Opened input MIDI device: %s"), devname); */
	AG_ThreadCreate(&thInput, VS_MidiInputThread, mid);
}
static void
SetOutputALSA(AG_Event *event)
{
	char devname[16];
	VS_Midi *mid = AG_PTR(1);
	int card = AG_INT(2);
	int dev = AG_INT(3);
	int subdev = AG_INT(4);
	int rv;

	if (mid->pvt->out != NULL) {
		AG_TextError(_("MIDI output device is already open"));
		return;
	}
	GetDeviceName(devname, sizeof(devname), card, dev, subdev);
 	if ((rv = snd_rawmidi_open(NULL, &mid->pvt->out, devname, 0)) != 0) {
		AG_TextError("ALSA: Failed to open %s: %s",
		    devname, snd_strerror(rv));
		return;
	}
	mid->flags |= VS_MIDI_OUTPUT;
/*	AG_LabelText(vsStatus, _("Opened output MIDI device: %s"), devname); */
	AG_ThreadCreate(&thOutput, VS_MidiOutputThread, mid);
}

static int
IsInputALSA(snd_ctl_t *ctl, int card, int device, int sub)
{
	snd_rawmidi_info_t *info;
	int rv;

	snd_rawmidi_info_alloca(&info);
	snd_rawmidi_info_set_device(info, device);
	snd_rawmidi_info_set_subdevice(info, sub);
	snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
	
	if ((rv = snd_ctl_rawmidi_info(ctl, info)) < 0 &&
	    rv != -ENXIO) {
		return (rv);
	} else if (rv == 0) {
		return (1);
	}
	return (0);
}
static int
IsOutputALSA(snd_ctl_t *ctl, int card, int device, int sub)
{
	snd_rawmidi_info_t *info;
	int rv;

	snd_rawmidi_info_alloca(&info);
	snd_rawmidi_info_set_device(info, device);
	snd_rawmidi_info_set_subdevice(info, sub);
	snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
	
	if ((rv = snd_ctl_rawmidi_info(ctl, info)) < 0 &&
	    rv != -ENXIO) {
		return (rv);
	} else if (rv == 0) {
		return (1);
	}
	return (0);
}

static int
MenuDevicesALSA(VS_Midi *mid, AG_MenuItem *m, snd_ctl_t *ctl, int card,
    int dev, Uint flags)
{
	char text[64];
	snd_rawmidi_info_t *info;
	const char *name;
	const char *sub_name;
	int subs, subs_in, subs_out;
	int sub, in, out;
	int rv;

	snd_rawmidi_info_alloca(&info);
	snd_rawmidi_info_set_device(info, dev);

	snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
	snd_ctl_rawmidi_info(ctl, info);
	subs_in = snd_rawmidi_info_get_subdevices_count(info);
	snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
	snd_ctl_rawmidi_info(ctl, info);
	subs_out = snd_rawmidi_info_get_subdevices_count(info);
	subs = subs_in > subs_out ? subs_in : subs_out;

	sub = 0;
	in = out = 0;
	if ((rv = IsOutputALSA(ctl, card, dev, sub)) < 0) {
		AG_SetError("ALSA device %d:%d: %s", card, dev, snd_strerror(rv));
		return (-1);
	} else if (rv) {
		out = 1;
	}

	if (rv == 0) {
		if ((rv = IsInputALSA(ctl, card, dev, sub)) < 0) {
			AG_SetError("ALSA device %d:%d: %s", card, dev, snd_strerror(rv));
			return (-1);
		}
	} else if (rv) {
		in = 1;
	}
	if (rv == 0)
		return (0);

	name = snd_rawmidi_info_get_name(info);
	sub_name = snd_rawmidi_info_get_subdevice_name(info);
	if (sub_name[0] == '\0') {
		snprintf(text, sizeof(text), "(%d,%d) %s", card, dev, name);
		if ((flags & VS_MIDI_INPUT) && in) {
			AG_MenuAction(m, text, vsIconControls.s, SetInputALSA,
			    "%p,%i,%i,%i",
			    mid, card, dev, -1);
		}
		if ((flags & VS_MIDI_OUTPUT) && out) {
			AG_MenuAction(m, text, vsIconControls.s, SetOutputALSA,
			    "%p,%i,%i,%i",
			    mid, card, dev, -1);
		}
	} else {
		sub = 0;
		for (;;) {
			snprintf(text, sizeof(text), "(%d,%d,%d) %s",
			    card, dev, sub, sub_name);
			if ((flags & VS_MIDI_INPUT) && in)
				AG_MenuAction(m, text, vsIconControls.s,
				    SetInputALSA, "%p,%i,%i,%i",
				    mid, card, dev, sub);
			if ((flags & VS_MIDI_OUTPUT) && out)
				AG_MenuAction(m, text, vsIconControls.s,
				    SetOutputALSA, "%p,%i,%i,%i",
				    mid, card, dev, sub);

			if (++sub >= subs)
				break;

			in = IsInputALSA(ctl, card, dev, sub);
			out = IsOutputALSA(ctl, card, dev, sub);
			snd_rawmidi_info_set_subdevice(info, sub);
			if (out) {
				snd_rawmidi_info_set_stream(info,
				    SND_RAWMIDI_STREAM_OUTPUT);
				if ((rv = snd_ctl_rawmidi_info(ctl, info)) < 0)
					break;
			} else {
				snd_rawmidi_info_set_stream(info,
				    SND_RAWMIDI_STREAM_INPUT);
				if ((rv = snd_ctl_rawmidi_info(ctl, info)) < 0)
					break;
			}
			sub_name = snd_rawmidi_info_get_subdevice_name(info);
		}
	}
	return (0);
}
#endif /* HAVE_ALSA */

void
VS_MidiDevicesMenu(VS_Midi *mid, AG_MenuItem *pm, Uint flags)
{
#ifdef HAVE_ALSA
	AG_MenuItem *m;
	int card = -1, rv;
	
	m = AG_MenuNode(pm, (flags & VS_MIDI_INPUT) ?
	                    _("MIDI Input") : _("MIDI Output"), NULL);

	if ((rv = snd_card_next(&card)) < 0) {
		AG_MenuSection(m, "(%s)", snd_strerror(rv));
		return;
	}
	if (card < 0) {
		AG_MenuSection(m, _("(No MIDI Devices)"));
		return;
	}

	do {
		snd_ctl_t *ctl;
		char dev[32];
		int i, rv;

		snprintf(dev, sizeof(dev), "hw:%d", card);
		if ((rv = snd_ctl_open(&ctl, dev, 0)) < 0) {
			AG_MenuSection(m, "(Opening: %s)", snd_strerror(rv));
			return;
		}
		for (i = -1; ; ) {
			if ((rv = snd_ctl_rawmidi_next_device(ctl, &i)) < 0) {
				AG_MenuSection(m, "(Getting device: %s)",
				    snd_strerror(rv));
				break;
			}
			if (i < 0) {
				break;
			}
			if (MenuDevicesALSA(mid, m, ctl, card, i, flags) == -1)
				AG_MenuSection(m, "(%s)", AG_GetError());
		}
		snd_ctl_close(ctl);

		if ((rv = snd_card_next(&card)) < 0) {
			AG_MenuSection(m, "(%s)", snd_strerror(rv));
			break;
		}
	} while (card >= 0);
#endif /* HAVE_ALSA */
}
