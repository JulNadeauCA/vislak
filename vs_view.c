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
	VS_Clip *v = vv->clip;
	VS_Project *vsp = v->proj;
	int dx = AG_INT(3);
	int xLast;

	if (v == NULL) {
		return;
	}
	if (vv->flags & VS_VIEW_PANNING) {
		if (dx < 0) {
			v->x++;
		} else if (dx > 0) {
			v->x--;
		}
		xLast = v->n - WIDTH(vv)/vsp->thumbSz;
		if (v->x > xLast)
			v->x = xLast;
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
	VS_Clip *v = vv->clip;

scan:
	for (i = 0; i < v->n; i++) {
		VS_Frame *vf = &v->frames[i];

		/* Look for a continuous selection. */
		for (j = i; j < v->n; j++) {
			if (!(v->frames[j].flags & VS_FRAME_SELECTED))
				break;
		}
		if (j == i) {
			continue;
		}
		VS_ClipDelFrames(v, i, j);
		nDeleted += (j-i);
		goto scan;
	}
	VS_Status(vv, _("Deleted %u frames"), nDeleted);
}

/* Select all frames */
static void
SelectAllFrames(VS_View *vv)
{
	VS_Clip *v = vv->clip;
	Uint i;

	for (i = 0; i < v->n; i++) {
		v->frames[i].flags |= VS_FRAME_SELECTED;
	}
	VS_Status(vv, _("Selected all frames"));
}

/* Unselect all frames */
static void
UnselectAllFrames(VS_View *vv)
{
	VS_Clip *v = vv->clip;
	Uint i;

	for (i = 0; i < v->n; i++) {
		v->frames[i].flags &= ~(VS_FRAME_SELECTED);
	}
	VS_Status(vv, _("Unselected all frames"));
}

/* Clear KBD keymap */
static void
ClearKeymapKBD(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Midi *mid;
	Uint nCleared;

	nCleared = VS_ClipClearKeys(vv->clip);
	VS_Status(vv, _("Cleared %u key mappings"), nCleared);
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
	VS_Status(vv, _("Cleared %u MIDI key mappings"), nCleared);
}

/* Create a default MIDI keymap scaled to the video length. */
static void
PartitionKeymapMIDI(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Clip *v = vv->clip;
	VS_Midi *mid;
	Uint i, j, key;
	Uint div, nMapped = 0;

	if ((mid = v->midi) == NULL) {
		return;
	}
	div = (Uint)v->n/60;
	key = 36;
	for (i = 0, j = 0;
	     i < v->n;
	     i++) {
		v->frames[i].midiKey = key;
		if (++j > div) {
			j = 0;
			key++;
		}
		mid->keymap[key] = i;
		nMapped++;
	}
	VS_Status(vv, _("Mapped %u MIDI keys"), nMapped);
}

/* Initialize a 1:1 MIDI keymap */
static void
InitKeymap11MIDI(AG_Event *event)
{
	VS_View *vv = AG_PTR(1);
	VS_Clip *v = vv->clip;
	VS_Midi *mid;
	Uint i, key;

	if ((mid = v->midi) == NULL) {
		return;
	}
	for (i = 0, key = 36;
	     i < v->n && key <= 96;
	     i++, key++) {
		v->frames[i].midiKey = key;
		mid->keymap[key] = i;
	}
	VS_Status(vv, _("Mapped %u MIDI keys"), i);
}

static void
PopupMenu(VS_View *vv, int x, int y)
{
	VS_Project *vsp = vv->clip->proj;
	AG_PopupMenu *pm;
	AG_MenuItem *m, *mMIDI, *mKeymaps, *mSub;

	pm = AG_PopupNew(vv);
	m = pm->root;

	AG_MenuUintFlagsMp(m, _("Key Learn Mode"), vsIconControls.s,
	    &vsp->flags, VS_PROJECT_LEARNING, 0, &OBJECT(vsp)->lock);

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
	VS_Clip *v = vv->clip;
	VS_Project *vsp = v->proj;
	int button = AG_INT(1);
	int x = AG_INT(2);
	int y = AG_INT(3);
	Uint ms = WIDGET(vv)->drv->kbd->modState;
	AG_Event ev;
	int f;

	if (v == NULL) {
		return;
	}
	switch (button) {
	case AG_MOUSE_WHEELUP:
		if (v->x > 0) { v->x--; }
		break;
	case AG_MOUSE_WHEELDOWN:
		if (v->x < (v->n - 1)) { v->x++; }
		break;
	case AG_MOUSE_LEFT:
		f = v->x + x/vsp->thumbSz;
		if (f >= 0 && f < v->n) {
			VS_Frame *vf = &v->frames[f];
			int i, fSel;

			if (ms & AG_KEYMOD_CTRL) {
				if (vf->flags & VS_FRAME_SELECTED) {
					vf->flags &= ~(VS_FRAME_SELECTED);
				} else {
					vf->flags |= VS_FRAME_SELECTED;
				} 
			} else if (ms & AG_KEYMOD_SHIFT) {
				for (fSel = 0; fSel < v->n; fSel++) {
					if (v->frames[fSel].flags &
					    VS_FRAME_SELECTED)
						break;
				}
				if (fSel == v->n) {
					fSel = 0;
				}
				if (f < fSel) {
					for (i = f; i < fSel; i++) {
						v->frames[i].flags
						    |= VS_FRAME_SELECTED;
					}
				} else {
					for (i = f; i > fSel; i--) {
						v->frames[i].flags
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
	VS_Clip *v = vv->clip;
	int x;

	if (v->n == 0) { return (0); }

	if (vv->kbdDir == -1) {
		if (--(vv->kbdOffset) < -10)
			vv->kbdDir = +1;
	} else {
		if (++(vv->kbdOffset) > 10)
			vv->kbdDir = -1;
	}

	x = vv->kbdCenter + vv->kbdOffset;
	if (x < 0) { x = 0; }
	if (x >= v->n) { x = v->n - 1; }
	v->x = x;

	AG_Redraw(vv);
	return (to->ival);
}

static void
KeyDown(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	VS_Clip *v = vv->clip;
	VS_Project *vsp = v->proj;
	int sym = AG_INT(1);
	int mod = AG_INT(2);

	if (v == NULL) {
		return;
	}
	if ((isalpha(sym) || isdigit(sym)) &&
	    (vsp->flags & VS_PROJECT_LEARNING) &&
	    vv->xSel >= 0 && vv->xSel < v->n) {
		v->kbdKeymap[sym] = vv->xSel;
		v->frames[vv->xSel].kbdKey = sym;
		VS_Status(vv, _("Mapped %d -> f%d"), sym, vv->xSel);
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
	}
	if (isalpha(sym) || isdigit(sym)) {
		if (v->kbdKeymap[sym] != -1) {
			if (vv->kbdCenter != -1) {
				AG_DelTimer(vv, &vv->toKbdMove);
			}
			vv->xSel = v->kbdKeymap[sym];
			vv->kbdCenter = v->kbdKeymap[sym];
			vv->kbdOffset = 0;
			vv->kbdDir = +1;
			AG_AddTimer(vv, &vv->toKbdMove, 5, KbdMoveTimeout, NULL);
		}
	}
}

static void
KeyUp(AG_Event *event)
{
	VS_View *vv = AG_SELF();
	VS_Clip *v = vv->clip;
	int sym = AG_INT(1);
	int mod = AG_INT(2);

	if (v == NULL) {
		return;
	}
	if (vv->kbdCenter != -1 && vv->kbdCenter == v->kbdKeymap[sym]) {
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
	vv->incr = 1;
	vv->clip = clip;
	vv->hPre = clip->proj->thumbSz;

	if (flags & VS_VIEW_HFILL) { AG_ExpandHoriz(vv); }
	if (flags & VS_VIEW_VFILL) { AG_ExpandVert(vv); }
	AGWIDGET(vv)->flags |= AG_WIDGET_FOCUSABLE;

	vv->sb = AG_ScrollbarNew(vv, AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_NOAUTOHIDE);
	AG_BindUint(vv->sb, "value", &vv->clip->x);
	AG_BindUint(vv->sb, "max", &vv->clip->n);
//	AG_BindUint(vv->sb, "visible", &vv->xVis);
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
	vv->clip = NULL;
	vv->wPre = 700;
	vv->hPre = 128;
	vv->xSel = -1;
	vv->xVis = 0;
	vv->rFrames = AG_RECT(0,0,0,0);
	vv->rAudio = AG_RECT(0,0,0,0);
	vv->sb = NULL;
	vv->incr = 10;
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
	VS_Project *vsp = vv->clip->proj;
	AG_SizeReq rBar;
	
	r->w = vv->wPre;
	r->h = vv->hPre + agTextFontHeight;
	if (!(vv->flags & VS_VIEW_NOAUDIO))
		vv->rFrames.h += vsp->waveSz;
}

static int
SizeAllocate(void *p, const AG_SizeAlloc *a)
{
	VS_View *vv = p;
	VS_Project *vsp = vv->clip->proj;
	AG_SizeAlloc aBar;
	int wSb;
	
	if (a->h < (wSb = agTextFontHeight) + 5)
		return (-1);

	vv->rFrames.x = 0;
	vv->rFrames.y = 0;
	vv->rFrames.w = a->w;
	if (!(vv->flags & VS_VIEW_NOAUDIO)) {
		vv->rFrames.h = MIN(vsp->thumbSz, a->h - wSb - vsp->waveSz);
	} else {
		vv->rFrames.h = MIN(vsp->thumbSz, a->h - wSb);
	}
	if (vv->rFrames.h < 0) { vv->rFrames.h = 0; }

	vv->rAudio.x = 0;
	vv->rAudio.y = vv->rFrames.h;
	vv->rAudio.w = a->w;

	if (!(vv->flags & VS_VIEW_NOAUDIO)) {
		vv->rAudio.h = MIN(vsp->waveSz, a->h - wSb - vv->rFrames.h);
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

	vv->xVis = a->w/vsp->thumbSz;
	return (0);
}

static void
Draw(void *p)
{
	VS_View *vv = p;
	VS_Clip *v = vv->clip;
	VS_Project *vsp = v->proj;
	AG_Rect r;
	AG_Color c;
	int i;

	if (vv->rFrames.h <= 0 && vv->rAudio.h <= 0)
		return;

	AG_MutexLock(&v->lock);
	
	/*
	 * Render video frames.
	 */
	AG_ColorRGB(&c, 100,100,100);
	AG_DrawBox(vv, &vv->rFrames, -1, &c);
	r = vv->rFrames;
	r.w = vsp->thumbSz;

	AG_PushTextState();
	AG_TextBGColorRGB(0,0,0);
	AG_TextColorRGB(255,255,255);

	AG_PushClipRect(vv, &vv->rFrames);
	for (i = v->x;
	     i < v->n && r.x < WIDTH(vv);
	     i++) {
		VS_Frame *vf = &v->frames[i];

		if (i < 0) {
			r.x += vf->thumb->w;
			continue;
		}

		AG_WidgetBlit(vv, vf->thumb, r.x, 0);
		if (vf->flags & VS_FRAME_SELECTED) {
			AG_ColorRGB(&c, 250,250,250);
			AG_DrawRectOutline(vv, &r, &c);
			AG_ColorRGBA(&c, 0,0,255,64);
			AG_DrawRectBlended(vv, &r, &c, AG_ALPHA_SRC,
			    AG_ALPHA_ONE_MINUS_SRC);
		}
		if (vf->kbdKey != -1) {
			AG_Surface *S;

			S = AG_TextRenderf("%c", (char)vf->kbdKey);
			AG_WidgetBlit(vv, S, r.x, 0);
			AG_SurfaceFree(S);
		}
		if (vf->midiKey != -1) {
			AG_Surface *S;

			S = AG_TextRenderf("%x", vf->midiKey);
			AG_WidgetBlit(vv, S, r.x, 0);
			AG_SurfaceFree(S);
		}
		r.x += vf->thumb->w;
	}
	AG_PopClipRect(vv);

	/*
	 * Render the audio waveform.
	 */
	if (vv->rAudio.h > 0 && v->sndViz != NULL) {
		Uint pos;
		int val;

		AG_ColorBlack(&c);
		AG_DrawBox(vv, &vv->rAudio, 1, &c);
		r = vv->rAudio;

		AG_PushClipRect(vv, &vv->rAudio);

		/* Center line */
		r.y += vsp->waveSz/2;
		AG_ColorRGB(&c, 0,50,250);
		AG_DrawLineH(vv, 0, WIDTH(vv), r.y, &c);

		/* Samples */
		AG_MutexLock(&v->sndLock);
		for (r.x = 0, pos = v->x*vsp->thumbSz;
		     r.x < WIDTH(vv) && pos < v->sndVizFrames;
		     r.x++) {
			val = (int)(v->sndViz[pos]*vsp->waveSz);
			if (val != 0) {
				AG_ColorRGB(&c, 0,250,0);
				AG_DrawLineV(vv, r.x,
				    r.y - val,
				    r.y + val, &c);
			}
			pos++;
		}
		AG_MutexUnlock(&v->sndLock);
		
		AG_PopClipRect(vv);
	}

	AG_PopTextState();

	AG_MutexUnlock(&v->lock);

	/* Render the scrollbar. */
	if (vv->sb != NULL) {
		if (v->n > 0 && vv->xVis > 0 &&
		    vv->xVis < v->n) {
			AG_ScrollbarSetControlLength(vv->sb,
			    (vv->xVis * vv->sb->length / v->n));
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
