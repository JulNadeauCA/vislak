/*
 * Copyright (c) 2010 Julien Nadeau (vedge@hypertriton.com).
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

#include <ctype.h>

#include <vislak.h>
#include "vs_view.h"

#include "icons.h"

/*
 * Visualization widget for video clips.
 */

static void
MouseMotion(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	int dx = AG_INT(3);
	int xLast;

	if (vv->clip == NULL) {
		return;
	}
	if (vv->flags & VS_VIEW_PANNING) {
		if (dx < 0) {
			vv->xOffs++;
		} else if (dx > 0) {
			vv->xOffs--;
		}
		if (vv->xOffs < 0)
			vv->xOffs = 0;

		xLast = vv->clip->n - WIDTH(vv)/vsThumbSz;
		if (vv->xOffs > xLast)
			vv->xOffs = xLast;
	}
}

static void
MouseButtonUp(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	int button = AG_INT(1);

	if (vv->clip == NULL) {
		return;
	}
	switch (button) {
	case AG_MOUSE_MIDDLE:
		vv->flags &= ~(VS_VIEW_PANNING);
		break;
	}
}

/* Delete the selected frames */
static void
DeleteFrames(VS_View *vv)
{
	Uint i, j, nDeleted = 0;

scan:
	for (i = 0; i < vv->clip->n; i++) {
		VS_Frame *vf = &vv->clip->frames[i];

		/* Look for a continuous selection. */
		for (j = i; j < vv->clip->n; j++) {
			if (!(vv->clip->frames[j].flags & VS_FRAME_SELECTED))
				break;
		}
		if (j == i) {
			continue;
		}
		VS_ClipDelFrames(vv->clip, i, j);
		nDeleted += (j-i);
		goto scan;
	}
	AG_LabelText(vsStatus, _("Deleted %u frames"), nDeleted);
}

/* Select all frames */
static void
SelectAllFrames(VS_View *vv)
{
	Uint i;

	for (i = 0; i < vv->clip->n; i++) {
		vv->clip->frames[i].flags |= VS_FRAME_SELECTED;
	}
	AG_LabelText(vsStatus, _("Selected all frames"));
}

/* Unselect all frames */
static void
UnselectAllFrames(VS_View *vv)
{
	Uint i;

	for (i = 0; i < vv->clip->n; i++) {
		vv->clip->frames[i].flags &= ~(VS_FRAME_SELECTED);
	}
	AG_LabelText(vsStatus, _("Unselected all frames"));
}

/* Clear KBD keymap */
static void
ClearKeymapKBD(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Midi *mid;
	Uint nCleared;

	nCleared = VS_ClipClearKeys(vv->clip);
	AG_LabelText(vsStatus, _("Cleared %u key mappings"), nCleared);
}

/* Clear MIDI keymap */
static void
ClearKeymapMIDI(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Midi *mid;
	Uint nCleared;

	if ((mid = vv->clip->midi) == NULL) {
		return;
	}
	nCleared = VS_MidiClearKeys(mid);
	AG_LabelText(vsStatus, _("Cleared %u MIDI key mappings"), nCleared);
}

/* Create a default MIDI keymap scaled to the video length. */
static void
PartitionKeymapMIDI(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Midi *mid;
	Uint i, j, key;
	Uint div, nMapped = 0;

	if ((mid = vv->clip->midi) == NULL) {
		return;
	}
	div = (Uint)vv->clip->n/60;
	key = 36;
	for (i = 0, j = 0;
	     i < vv->clip->n;
	     i++) {
		vv->clip->frames[i].midiKey = key;
		if (++j > div) {
			j = 0;
			key++;
		}
		mid->keymap[key] = i;
		nMapped++;
	}
	AG_LabelText(vsStatus, _("Mapped %u MIDI keys"), nMapped);
}

/* Initialize a 1:1 MIDI keymap */
static void
InitKeymap11MIDI(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Midi *mid;
	Uint i, key;

	if ((mid = vv->clip->midi) == NULL) {
		return;
	}
	for (i = 0, key = 36;
	     i < vv->clip->n && key <= 96;
	     i++, key++) {
		vv->clip->frames[i].midiKey = key;
		mid->keymap[key] = i;
	}
	AG_LabelText(vsStatus, _("Mapped %u MIDI keys"), i);
}

static void
PopupMenu(VS_View *vv, int x, int y)
{
	AG_PopupMenu *pm = AG_PopupNew(vv);
	AG_MenuItem *m = pm->item;
	AG_MenuItem *mMIDI, *mKeymaps, *mSub;
	
	AG_MenuBoolMp(m, _("Key Learn Mode"), vsIconControls.s, /* XXX icon */
	    &vsLearning, 0, &vsProcLock);

	AG_MenuSeparator(m);

	mMIDI = AG_MenuNode(m, _("MIDI"), NULL);
	{
		VS_MidiDevicesMenu(vv->clip->midi, mMIDI, VS_MIDI_INPUT);
		VS_MidiDevicesMenu(vv->clip->midi, mMIDI, VS_MIDI_OUTPUT);
	}
	
	mKeymaps = AG_MenuNode(m, _("Keymaps"), NULL);
	{
		mSub = AG_MenuNode(mKeymaps, _("KBD Keymap"), NULL);
		{
			AG_MenuAction(mSub, _("Clear keymap"), agIconTrash.s,
			    ClearKeymapKBD, "%p", vv);
		}
		mSub = AG_MenuNode(mKeymaps, _("MIDI Keymap"), NULL);
		{
			AG_MenuAction(mSub, _("Clear keymap"), agIconTrash.s,
			    ClearKeymapMIDI, "%p", vv);
			AG_MenuAction(mSub, _("Partition keymap"), vsIconControls.s,
			    PartitionKeymapMIDI, "%p", vv);
			AG_MenuAction(mSub, _("Initialize 1:1 keymap"), vsIconControls.s,
			    InitKeymap11MIDI, "%p", vv);
		}
	}

	AG_PopupShowAt(pm, x, y);
}

static void
MouseButtonDown(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	int button = AG_INT(1);
	int x = AG_INT(2);
	int y = AG_INT(3);
	Uint ms = WIDGET(vv)->drv->kbd->modState;
	AG_Event ev;
	int f;

	if (vv->clip == NULL) {
		return;
	}
	switch (button) {
	case AG_MOUSE_WHEELUP:
		if (vv->xOffs > 0) { vv->xOffs--; }
		break;
	case AG_MOUSE_WHEELDOWN:
		if (vv->xOffs < (vv->clip->n-1)) { vv->xOffs++; }
		break;
	case AG_MOUSE_LEFT:
		f = vv->xOffs + x/vsThumbSz;
		if (f >= 0 && f < vv->clip->n) {
			VS_Frame *vf = &vv->clip->frames[f];
			int i, fSel;

			if (ms & AG_KEYMOD_CTRL) {
				if (vf->flags & VS_FRAME_SELECTED) {
					vf->flags &= ~(VS_FRAME_SELECTED);
				} else {
					vf->flags |= VS_FRAME_SELECTED;
				} 
			} else if (ms & AG_KEYMOD_SHIFT) {
				for (fSel = 0; fSel < vv->clip->n; fSel++) {
					if (vv->clip->frames[fSel].flags &
					    VS_FRAME_SELECTED)
						break;
				}
				if (fSel == vv->clip->n) {
					fSel = 0;
				}
				if (f < fSel) {
					for (i = f; i < fSel; i++) {
						vv->clip->frames[i].flags
						    |= VS_FRAME_SELECTED;
					}
				} else {
					for (i = f; i > fSel; i--) {
						vv->clip->frames[i].flags
						    |= VS_FRAME_SELECTED;
					}
				}
			} else {
				UnselectAllFrames(vv);
				vf->flags |= VS_FRAME_SELECTED;
			}
			vv->xSel = f;
		}
		AG_WidgetFocus(vv);
		break;
	case AG_MOUSE_MIDDLE:
		AG_WidgetFocus(vv);
		vv->flags |= VS_VIEW_PANNING;
		break;
	case AG_MOUSE_RIGHT:
		PopupMenu(vv, x, y);
		break;
	}
}

static Uint32
KbdMoveTimeout(AG_Timer *to, AG_Event *event)
{
	VS_View *vv = AG_SELF();
	int xOffs;

	if (vv->clip->n == 0) { return (0); }

	xOffs = vv->kbdCenter + (int)(sin(vv->kbdVal)*20.0);
	if (xOffs < 0) { xOffs = 0; }
	if (xOffs >= vv->clip->n) { xOffs = vv->clip->n - 1; }
	vv->xOffs = xOffs;
	vv->kbdVal += 0.05;

	return (to->ival);
}

static void
KeyDown(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	VS_Clip *vc = vv->clip;
	int sym = AG_INT(1);
	int mod = AG_INT(2);

	if (vc == NULL) {
		return;
	}
	if ((isalpha(sym) || isdigit(sym)) &&
	    vsLearning &&
	    vv->xSel >= 0 && vv->xSel < vc->n) {
		vc->kbdKeymap[sym] = vv->xSel;
		vc->frames[vv->xSel].kbdKey = sym;
		AG_LabelText(vsStatus, _("Mapped %d -> f%d"), sym, vv->xSel);
		return;
	}
	switch (sym) {
	case AG_KEY_A:
		if (mod & AG_KEYMOD_CTRL) {
			SelectAllFrames(vv);
			return;
		}
		break;
	case AG_KEY_U:
		if (mod & AG_KEYMOD_CTRL) {
			UnselectAllFrames(vv);
			return;
		}
		break;
	case AG_KEY_DELETE:
		DeleteFrames(vv);
		return;
	default:
		break;
	}
	if (isalpha(sym) || isdigit(sym)) {
		if (vc->kbdKeymap[sym] != -1) {
			if (vv->kbdCenter != -1) {
				AG_DelTimer(vv, &vv->toKbdMove);
			}
			vv->xSel = vc->kbdKeymap[sym];
			vv->kbdCenter = vc->kbdKeymap[sym];
			vv->kbdVal = 0.0;
			AG_AddTimer(vv, &vv->toKbdMove, 5,
			    KbdMoveTimeout, NULL);
		}
	}
}

static void
KeyUp(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	VS_Clip *vc = vv->clip;
	int sym = AG_INT(1);
	int mod = AG_INT(2);

	if (vc == NULL) {
		return;
	}
	if (vv->kbdCenter != -1 && vv->kbdCenter == vc->kbdKeymap[sym]) {
		AG_DelTimer(vv, &vv->toKbdMove);
		vv->kbdCenter = -1;
	}
}

VS_View *
VS_ViewNew(void *parent, Uint flags, VS_Clip *clip)
{
	VS_View *vv;

	vv = Malloc(sizeof(VS_View));
	AG_ObjectInit(vv, &vsViewClass);
	vv->flags |= flags;
	vv->clip = clip;
	vv->incr = 1;

	if (flags & VS_VIEW_HFILL) { AG_ExpandHoriz(vv); }
	if (flags & VS_VIEW_VFILL) { AG_ExpandVert(vv); }
	AGWIDGET(vv)->flags |= AG_WIDGET_FOCUSABLE;

	vv->sb = AG_ScrollbarNew(vv, AG_SCROLLBAR_HORIZ, 0);
	AG_BindUint(vv->sb, "value", &vv->xOffs);
	AG_BindUint(vv->sb, "max", &vv->clip->n);
	AG_BindUint(vv->sb, "visible", &vv->xVis);
	AG_SetUint(vv->sb, "inc", 10);
	AG_WidgetSetFocusable(vv->sb, 0);

	/* Tie the clip's MIDI settings to this view. */
	if (!(flags & VS_VIEW_NOMIDI)) {
		if (clip->midi == NULL) {
			clip->midi = VS_MidiNew(vv);
		} else {
			clip->midi->vv = vv;
		}
	}
	
	AG_SetEvent(vv, "mouse-button-down", MouseButtonDown, NULL);
	AG_SetEvent(vv, "mouse-button-up", MouseButtonUp, NULL);
	AG_SetEvent(vv, "mouse-motion", MouseMotion, NULL);
	AG_SetEvent(vv, "key-down", KeyDown, NULL);
	AG_SetEvent(vv, "key-up", KeyUp, NULL);

	AG_ObjectAttach(parent, vv);
	return (vv);
}

void
VS_ViewSetIncrement(VS_View *vv, int incr)
{
	AG_ObjectLock(vv);
	vv->incr = incr;
	AG_ObjectUnlock(vv);
}

static void
Init(void *obj)
{
	VS_View *vv = obj;

	vv->flags = 0;
	vv->wPre = 700;
	vv->hPre = vsThumbSz;
	vv->xOffs = 0;
	vv->xSel = -1;
	vv->xVis = 0;
	vv->rFrames = AG_RECT(0,0,0,0);
	vv->rAudio = AG_RECT(0,0,0,0);
	vv->sb = NULL;
	vv->incr = 10;
	vv->clip = NULL;
	vv->xVel = 0.0;
	vv->kbdCenter = -1;
}

void
VS_ViewSizeHint(VS_View *vv, Uint w, Uint h)
{
	AG_ObjectLock(vv);
	vv->wPre = w;
	vv->hPre = h;
	AG_ObjectUnlock(vv);
}

static void
SizeRequest(void *p, AG_SizeReq *r)
{
	VS_View *vv = p;
	AG_SizeReq rBar;
	
	r->w = vv->wPre;
	r->h = vv->hPre + agTextFontHeight;
	if (!(vv->flags & VS_VIEW_NOAUDIO))
		vv->rFrames.h += vsWaveSz;
}

static int
SizeAllocate(void *p, const AG_SizeAlloc *a)
{
	VS_View *vv = p;
	AG_SizeAlloc aBar;
	int wSb;
	
	if (a->h < (wSb = agTextFontHeight) + 5)
		return (-1);

	vv->rFrames.x = 0;
	vv->rFrames.y = 0;
	vv->rFrames.w = a->w;
	if (!(vv->flags & VS_VIEW_NOAUDIO)) {
		vv->rFrames.h = MIN(vsThumbSz, a->h - wSb - vsWaveSz);
	} else {
		vv->rFrames.h = MIN(vsThumbSz, a->h - wSb);
	}
	if (vv->rFrames.h < 0) { vv->rFrames.h = 0; }

	vv->rAudio.x = 0;
	vv->rAudio.y = vv->rFrames.h;
	vv->rAudio.w = a->w;

	if (!(vv->flags & VS_VIEW_NOAUDIO)) {
		vv->rAudio.h = MIN(vsWaveSz, a->h - wSb - vv->rFrames.h);
		if (vv->rAudio.h < 0) { vv->rAudio.h = 0; }
	} else {
		vv->rAudio.h = 0;
	}

	if (vv->sb != NULL) {
		aBar.w = a->w;
		aBar.h = wSb;
		aBar.x = 0;
		aBar.y = a->h - wSb;
		AG_WidgetSizeAlloc(vv->sb, &aBar);
	}

	vv->xVis = a->w/vsThumbSz;
	return (0);
}

static void
Draw(void *p)
{
	VS_View *vv = p;
	VS_Clip *vc = vv->clip;
	AG_Rect r;
	int i;

	if (vv->rFrames.h <= 0 && vv->rAudio.h <= 0)
		return;

	AG_MutexLock(&vc->lock);
	
	/*
	 * Render video frames.
	 */
	AG_DrawBox(vv, vv->rFrames, -1, AG_ColorRGB(100,100,100));
	r = vv->rFrames;
	r.w = vsThumbSz;

	AG_PushTextState();
	AG_TextBGColorRGB(0,0,0);
	AG_TextColorRGB(255,255,255);

	AG_PushClipRect(vv, vv->rFrames);
	for (i = vv->xOffs;
	     i >= 0 && i < vv->clip->n && r.x < WIDTH(vv);
	     i++) {
		VS_Frame *vf = &vc->frames[i];

		AG_WidgetBlit(vv, vf->thumb, r.x, 0);
		if (vf->flags & VS_FRAME_SELECTED) {
			AG_DrawRectOutline(vv, r,
			    AG_ColorRGB(250,250,250));
			AG_DrawRectBlended(vv, r,
			    AG_ColorRGBA(0,0,255,64),
			    AG_ALPHA_SRC);
		}
		if (vf->kbdKey != -1) {
			AG_Surface *s;
			s = AG_TextRenderf("%c", (char)vf->kbdKey);
			AG_WidgetBlit(vv, s, r.x, 0);
			AG_SurfaceFree(s);
		}
		if (vf->midiKey != -1) {
			AG_Surface *s;
			s = AG_TextRenderf("%x", vf->midiKey);
			AG_WidgetBlit(vv, s, r.x, 0);
			AG_SurfaceFree(s);
		}
		r.x += vf->thumb->w;
	}
	AG_PopClipRect(vv);

	/*
	 * Render the audio waveform.
	 */
	if (vv->rAudio.h > 0 && vc->sndViz != NULL) {
		double signal[2];
		Uint pos;
		int val;

		AG_DrawBox(vv, vv->rAudio, 1, AG_ColorRGB(0,0,0));
		r = vv->rAudio;

		AG_PushClipRect(vv, vv->rAudio);

		/* Center line */
		r.y += vsWaveSz/2;
		AG_DrawLineH(vv, 0, WIDTH(vv), r.y, AG_ColorRGB(0,50,250));

		/* Samples */
		AG_MutexLock(&vc->sndLock);
		for (r.x = 0, pos = vv->xOffs*vsThumbSz;
		     r.x < WIDTH(vv) && pos >= 0 && pos < vc->sndVizFrames;
		     r.x++) {
			val = (int)(vc->sndViz[pos]*vsWaveSz);
			if (val != 0) {
				AG_DrawLineV(vv, r.x,
				    r.y - val,
				    r.y + val,
				    AG_ColorRGB(0,250,0));
			}
			pos++;
		}
		AG_MutexUnlock(&vc->sndLock);
		
		AG_PopClipRect(vv);
	}

	AG_PopTextState();

	AG_MutexUnlock(&vc->lock);

	/* Render the scrollbar. */
	if (vv->sb != NULL) {
		if (vv->clip->n > 0 && vv->xVis > 0 &&
		    vv->xVis < vv->clip->n) {
			AG_ScrollbarSetControlLength(vv->sb,
			    (vv->xVis * vv->sb->length / vv->clip->n));
		} else {
			AG_ScrollbarSetControlLength(vv->sb, -1);
		}
		AG_WidgetDraw(vv->sb);
	}
}

AG_WidgetClass vsViewClass = {
	{
		"AG_Widget:VS_View",
		sizeof(VS_View),
		{ 0,0 },
		Init,
		NULL,			/* free */
		NULL,			/* destroy */
		NULL,			/* load */
		NULL,			/* save */
		NULL			/* edit */
	},
	Draw,
	SizeRequest,
	SizeAllocate
};
