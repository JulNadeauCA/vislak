/*
 * Copyright (c) 2013 Hypertriton, Inc. <http://hypertriton.com/>
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
 * Vislak project.
 */

#include <vislak.h>
#include "vs_gui.h"
#include "icons.h"

#include <math.h>
#include <unistd.h>
#include <errno.h>

/* Load audio stream. */
static int
LoadAudio(VS_Clip *v)
{
	VS_Project *vsp = v->proj;
	sf_count_t nReadFrames = 0;
	sf_count_t i, j, frames_px, pos;
	int tri = 0, tflag = 0;

	if (v->sndFile != NULL) {
		sf_close(v->sndFile);
		Free(v->sndBuf); v->sndBuf = NULL;
		Free(v->sndViz); v->sndViz = NULL;
		v->sndVizFrames = 0;
	}
	memset(&v->sndInfo, 0, sizeof(v->sndInfo));
	v->sndPos = 0;
	v->sndFile = sf_open(v->audioFile, SFM_READ, &v->sndInfo);
	if (v->sndFile == NULL) {
		AG_SetError("Cannot open %s", v->audioFile);
		return (-1);
	}
	if ((v->sndBuf = AG_TryMalloc(v->sndInfo.frames*v->sndInfo.channels*
	                              sizeof(float))) == NULL) {
		goto fail;
	}
	nReadFrames = 0;
	while (nReadFrames < v->sndInfo.frames) {
		sf_count_t rv;

		rv = sf_readf_float(v->sndFile, &v->sndBuf[nReadFrames], 4096);
		if (rv == 0) {
			break;
		}
		nReadFrames += rv;
	}

	/* Compute the approximate number of audio samples per video frame */
	v->samplesPerFrame = v->sndInfo.samplerate/vsp->frameRate;

	/* Compute a reduced waveform for visualization purposes. */
	if (sf_command(v->sndFile, SFC_CALC_SIGNAL_MAX, &v->sndPeakSignal,
	    sizeof(v->sndPeakSignal)) != 0) {
		v->sndPeakSignal = 20000.0;
	}
	frames_px = v->samplesPerFrame/vsp->thumbSz;
	v->sndVizFrames = v->sndInfo.frames/frames_px;
	if ((v->sndViz = AG_TryMalloc(v->sndVizFrames*sizeof(float))) == NULL) {
		goto fail;
	}
	for (i = 0; i < v->sndVizFrames; i++) {
		v->sndViz[i] = 0.0;
	}
	v->sndPeakSignal /= 10000.0;
	for (i = 0, pos = 0;
	     i < v->sndVizFrames && (pos + frames_px*v->sndInfo.channels) < nReadFrames;
	     i++) {
		for (j = 0; j < frames_px*v->sndInfo.channels; j++) {
			v->sndViz[i] += MAX(v->sndViz[i],
			    fabs(v->sndBuf[pos++]/v->sndPeakSignal));
		}
		v->sndViz[i] /= frames_px*v->sndInfo.channels;
	}

	VS_Status(vsp,
	    _("Audio import successful (%d-Ch, %dHz, %lu frames)"),
	    v->sndInfo.channels,
	    v->sndInfo.samplerate,
	    (Ulong)nReadFrames);
	return (0);
fail:
	sf_close(v->sndFile);
	v->sndFile = NULL;
	Free(v->sndBuf);
	Free(v->sndViz);
	return (-1);
}

/* Load a clip's video frames. */
static int
LoadVideoFrames(VS_Clip *v)
{
	VS_Project *vsp = v->proj;
	char path[AG_PATHNAME_MAX];
	AG_Dir *d;
	Uint i;
	int fileOk;
	
	vsp->gui.progress.min = 0;
	vsp->gui.progress.max = 0;
	vsp->gui.progress.val = 0;

	AG_MutexLock(&v->lock);

	if ((d = AG_OpenDir(v->dir)) == NULL) {
		AG_MutexUnlock(&v->lock);
		return (-1);
	}
	vsp->gui.progress.val = v->fileFirst;
	vsp->gui.progress.max = (v->fileLast != -1) ? v->fileLast : d->nents;
	AG_CloseDir(d);

	i = v->fileFirst;
	do {
		Snprintf(path, sizeof(path), v->fileFmt, v->dir, i);

		if (AG_FileExists(path) == -1) {
			fileOk = 0;
		} else {
			AG_MutexUnlock(&v->lock);
			VS_Status(vsp, _("Importing: %s"), path);
			fileOk = (VS_ClipAddFrame(v, path) == 0) ? 1 : 0;
			AG_MutexLock(&v->lock);
		}
		vsp->gui.progress.val++;
		i++;
	} while (fileOk && (v->fileLast == -1 || i < v->fileLast));
	if (i > 0) { v->x = 1; }

	VS_Status(vsp, _("Loaded %u video frames"), v->n);
	vsp->gui.progress.val = vsp->gui.progress.max;
	
	AG_MutexUnlock(&v->lock);
	return (0);
}

void
VS_ProjectRunOperation(VS_Project *vsp, VS_ProcOp op)
{
	AG_ObjectLock(vsp);
	vsp->procOp = op;
	AG_ObjectUnlock(vsp);
}

/* Process one output frame while in recording mode. */
static void
ProcessRecording(VS_Project *vsp)
{
	char pathIn[AG_PATHNAME_MAX];
	char pathOut[AG_PATHNAME_MAX];
	VS_Clip *vIn = vsp->input;
	VS_Clip *vOut = vsp->output;
	Uint i;

	AG_MutexLock(&vOut->lock);
	AG_MutexLock(&vIn->lock);
	if (VS_ClipCopyFrame(vOut, vIn, vIn->x) == -1) {
		goto stop;
	}
	/* Format: %s,%u */
	snprintf(pathIn, sizeof(pathIn), vIn->fileFmt,
	    vIn->dir, (vIn->x + 1));
	snprintf(pathOut, sizeof(pathOut), vOut->fileFmt,
	    vOut->dir, (vOut->n - 1));
relink:
	if (link(pathIn, pathOut) == -1) {
		if (errno == EEXIST) {
			unlink(pathOut);
			goto relink;
		}
		AG_SetError("link(%s -> %s): %s", pathIn, pathOut,
		    AG_Strerror(errno));
		goto stop;
	}
	AG_MutexUnlock(&vIn->lock);
	AG_MutexUnlock(&vOut->lock);
	return;
stop:
	VS_Status(vsp, _("Recording interrupted: %s"), AG_GetError());
	vsp->flags &= ~(VS_PROJECT_RECORDING);
	AG_MutexUnlock(&vIn->lock);
	AG_MutexUnlock(&vOut->lock);
}

/*
 * Processing thread for asynchronous per-project operations.
 */
static void *
ProcessThread(void *pProj)
{
	VS_Project *vsp = pProj;
	Uint32 t1, t2 = 0;
	int rCur = 0, delta;
	
	t1 = AG_GetTicks();
	for (;;) {
		VS_Clip *vIn, *vOut;

		t2 = AG_GetTicks();
		AG_ObjectLock(vsp);
		vIn = vsp->input;
		vOut = vsp->output;

		if (vsp->procOp == VS_PROC_IDLE &&	     /* Video update */
		    (t2 - t1) >= (1000/vsp->frameRate)) {

			if (vsp->flags & VS_PROJECT_RECORDING) {
				ProcessRecording(vsp);
			}
			vOut->samplesPerFrame = vOut->sndInfo.samplerate /
			                        vsp->frameRate;

			/* Process frame movement */
			if (vIn->xVel < -1.0 ||
			    vIn->xVel > +1.0) {		/* >=1 frame */
				delta = (int)vIn->xVel;
				if ((vIn->x+delta) < vIn->n) {
					vIn->x += (int)vIn->xVel;
				}
			} else if (vIn->xVel != 0.0) {	/* Sub-frame */
				vIn->xVelCur += vIn->xVel;
				if (vIn->xVelCur <= -1.0 ||
				    vIn->xVelCur >= 1.0) {
					delta = (vIn->xVelCur < 0) ? -1 : 1;
					vIn->xVelCur = 0.0;
					if ((vIn->x+delta) < vIn->n)
						vIn->x += delta;
				}
			}

			VS_PlayerUpdate(vsp->gui.playerIn);
			VS_PlayerUpdate(vsp->gui.playerOut);

			if (vsp->flags & VS_PROJECT_RECORDING) {
				if (vOut->n > 1)
					vOut->x++;
			}

			/* Update the effective refresh rate */
			t1 = AG_GetTicks();
			rCur = (1000/vsp->frameRate) - (t1-t2);
			if (rCur < 1) { rCur = 1; }
		} else {
			switch (vsp->procOp) {
			case VS_PROC_LOAD_VIDEO:
				AG_ObjectUnlock(vsp);
				if (LoadVideoFrames(vIn) == -1) {
					VS_Status(vsp,
					    _("Video import failed: %s"),
					    AG_GetError());
				}
				AG_ObjectLock(vsp);
				vsp->procOp = (vIn->audioFile != NULL) ?
				    VS_PROC_LOAD_AUDIO :
				    VS_PROC_IDLE;
				break;
			case VS_PROC_LOAD_AUDIO:
				AG_ObjectUnlock(vsp);
				if (LoadAudio(vOut) == -1) {
					VS_Status(vsp,
					    _("Audio import failed: %s"),
					    AG_GetError());
				}
				AG_ObjectLock(vsp);
				vsp->procOp = VS_PROC_IDLE;
				break;
			case VS_PROC_TERMINATE:
				VS_Status(vsp, _("Terminating"));
				vsp->procOp = VS_PROC_INIT;
				AG_ObjectUnlock(vsp);
				goto out;
			default:
				AG_ObjectUnlock(vsp);
				AG_Delay(1);
				AG_ObjectLock(vsp);
				break;
			}
		}
		AG_ObjectUnlock(vsp);
	}
out:
	AG_ThreadExit(NULL);
}

/*
 * Load video stream.
 */
static void
LoadVideoJPEG(AG_Event *event)
{
	VS_Clip *v = AG_PTR(1);
	char *path = AG_STRING(2);
	char *s;

	AG_MutexLock(&v->lock);
	Free(v->dir);
	v->dir = Strdup(path);
	if ((s = strrchr(v->dir, PATHSEPC)) != NULL) {
		*s = '\0';
	}
	VS_ProjectRunOperation(v->proj, VS_PROC_LOAD_VIDEO);
	AG_MutexUnlock(&v->lock);
}
static void
LoadVideoDlg(AG_Event *event)
{
	VS_Clip *v = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Import video..."));
	fd = AG_FileDlgNewMRU(win, "vislak.mru.video",
	    AG_FILEDLG_LOAD|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);

	AG_FileDlgAddType(fd, _("JPEG (select first frame)"), "*.jpg,*.jpeg",
	    LoadVideoJPEG, "%p", v);

	AG_WindowShow(win);
}

/*
 * Load audio stream.
 */
static void
LoadAudioFile(AG_Event *event)
{
	VS_Clip *v = AG_PTR(1);
	char *path = AG_STRING(2);

	AG_MutexLock(&v->lock);
	Free(v->audioFile);
	v->audioFile = Strdup(path);
	VS_ProjectRunOperation(v->proj, VS_PROC_LOAD_AUDIO);
	AG_MutexUnlock(&v->lock);
}
static void
LoadAudioDlg(AG_Event *event)
{
	char extn[64];
	VS_Clip *v = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;
	SF_FORMAT_INFO fmt;
	int k, count;

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Import audio..."));
	fd = AG_FileDlgNewMRU(win, "vislak.mru.audio",
	    AG_FILEDLG_LOAD|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);

	sf_command(NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof(int));
	for (k = 0; k < count; k++) {
		fmt.format = k;
		sf_command(NULL, SFC_GET_FORMAT_MAJOR, &fmt, sizeof(fmt));

		extn[0] = '.';
		extn[1] = '\0';
		Strlcat(extn, fmt.extension, sizeof(extn));
		AG_FileDlgAddType(fd, fmt.name, extn,
		    LoadAudioFile, "%p", v);
	
		/* Map extra extensions */
		if (strcmp("OGG (OGG Container format)", fmt.name) == 0) {
			AG_FileDlgAddType(fd, fmt.name, ".ogg",
			    LoadAudioFile, "%p", v);
		}
	}
	AG_WindowShow(win);
}

static void
SaveToMPEG4(AG_Event *event)
{
//	VS_Clip *v = AG_PTR(1);
//	char *path = AG_STRING(2);

	/* TODO */
	/* VS_ClipSetArchivePath(v, path); */
}

static void
SaveVideoDlg(AG_Event *event)
{
	VS_Clip *v = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Export video as..."));
	fd = AG_FileDlgNewMRU(win, "vislak.mru.videos",
	    AG_FILEDLG_SAVE|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);
	AG_FileDlgSetOptionContainer(fd, AG_BoxNewVert(win, AG_BOX_HFILL));

	AG_FileDlgAddType(fd, _("MPEG-4"), "*.mpeg", SaveToMPEG4, "%p", v);

	AG_WindowShow(win);
}

VS_Project *
VS_ProjectNew(void *parent, const char *name)
{
	VS_Project *vsp;

	if ((vsp = TryMalloc(sizeof(VS_Project))) == NULL) {
		return (NULL);
	}
	AG_ObjectInitNamed(vsp, &vsProjectClass, name);
	if ((vsp->input = VS_ClipNew(vsp)) == NULL ||
	    (vsp->output = VS_ClipNew(vsp)) == NULL) {
		goto fail;
	}
	AG_ObjectAttach(parent, vsp);
	return (vsp);
fail:
	AG_ObjectDestroy(vsp);
	return (NULL);
}

void
VS_Status(void *obj, const char *fmt, ...)
{
	va_list ap;
	AG_Label *lbl;
	char *s;

	if (AG_OfClass(obj, "AG_Widget:VS_View")) {
		lbl = ((VS_View *)obj)->clip->proj->gui.status;
	} else if (AG_OfClass(obj, "VS_Project")) {
		lbl = ((VS_Project *)obj)->gui.status;
	} else {
		return;
	}
	va_start(ap, fmt);
	Vasprintf(&s, fmt, ap);
	va_end(ap);
	AG_LabelTextS(lbl, s);
	free(s);
}

static void
OnAttach(AG_Event *event)
{
	VS_Project *vsp = AG_SELF();

	AG_ThreadCreate(&vsp->procTh, ProcessThread, vsp);
}

static void
OnDetach(AG_Event *event)
{
	VS_Project *vsp = AG_SELF();

	vsp->procOp = VS_PROC_TERMINATE;
	while (vsp->procOp != VS_PROC_INIT) {
		AG_ObjectUnlock(vsp);
		AG_Delay(10);
		AG_ObjectLock(vsp);
	}
}

static void
Init(void *obj)
{
	VS_Project *vsp = obj;

	vsp->flags = 0;
	vsp->thumbSz = 128;
	vsp->waveSz = 64;
	vsp->frameRate = 30;
	vsp->bendSpeed = 2.0;
	vsp->bendSpeedMax = 40.0;
	vsp->input = NULL;
	vsp->output = NULL;
	vsp->procOp = VS_PROC_INIT;

	vsp->gui.progress.val = 0;
	vsp->gui.progress.min = 0;
	vsp->gui.progress.max = 0;
	vsp->gui.playerIn = NULL;
	vsp->gui.playerOut = NULL;
	vsp->gui.status = NULL;

	AG_SetEvent(vsp, "attached", OnAttach, NULL);
	AG_SetEvent(vsp, "detached", OnDetach, NULL);
}

static void
Destroy(void *obj)
{
	VS_Project *vsp = obj;

	if (vsp->input != NULL)
		VS_ClipDestroy(vsp->input);
	if (vsp->output != NULL)
		VS_ClipDestroy(vsp->output);
}

static int
Load(void *obj, AG_DataSource *ds, const AG_Version *ver)
{
	VS_Project *vsp = obj;

	vsp->flags &= ~(VS_PROJECT_SAVED);
	vsp->flags |= (AG_ReadUint32(ds) & VS_PROJECT_SAVED);
	vsp->thumbSz = (int)AG_ReadUint16(ds);
	vsp->waveSz = (int)AG_ReadUint16(ds);
	vsp->frameRate = (int)AG_ReadUint8(ds);
	vsp->bendSpeed = AG_ReadDouble(ds);
	vsp->bendSpeedMax = AG_ReadDouble(ds);
	return (0);
}

static int
Save(void *obj, AG_DataSource *ds)
{
	VS_Project *vsp = obj;

	AG_WriteUint32(ds, vsp->flags & VS_PROJECT_SAVED);
	AG_WriteUint16(ds, (Uint16)vsp->thumbSz);
	AG_WriteUint16(ds, (Uint16)vsp->waveSz);
	AG_WriteUint8(ds, (Uint8)vsp->frameRate);
	AG_WriteDouble(ds, vsp->bendSpeed);
	AG_WriteDouble(ds, vsp->bendSpeedMax);
	return (0);
}

static void *
Edit(void *obj)
{
	VS_Project *vsp = obj;
	VS_Clip *vIn = vsp->input;
	VS_Clip *vOut = vsp->output;
	AG_Mutex *lock = &OBJECT(vsp)->lock;
	AG_Window *win;
	AG_Box *boxStatus, *boxParams;
	AG_Statusbar *sb;
	AG_ProgressBar *pb;
	AG_Numerical *num;
	AG_Notebook *nb;
	AG_NotebookTab *ntab;
	AG_Label *lbl;
	AG_Menu *menu;
	AG_Pane *pa, *paHoriz;
	AG_MenuItem *m, *mNode;
	VS_View *vv;

	if ((win = AG_WindowNew(AG_WINDOW_MAIN)) == NULL) {
		return (NULL);
	}
	AG_WindowSetCaption(win, _("Vislak <%s>"), OBJECT(vsp)->name);
	menu = AG_MenuNew(win, AG_MENU_HFILL);
	paHoriz = AG_PaneNewHoriz(win, AG_PANE_EXPAND);
	pa = AG_PaneNewVert(paHoriz->div[0], AG_PANE_EXPAND);
	{
		AG_LabelNewPolled(pa->div[0], AG_LABEL_HFILL,
		    _("Input (%u frames):"), &vIn->n);
		vv = VS_ViewNew(pa->div[0], VS_VIEW_NOAUDIO|VS_VIEW_EXPAND, vIn);
		AG_WidgetFocus(vv);
	}
	{
		AG_LabelNewPolled(pa->div[0], AG_LABEL_HFILL,
		    _("Output (%u frames):"), &vOut->n);
		vv = VS_ViewNew(pa->div[1], VS_VIEW_EXPAND, vOut);
	}
	
	nb = AG_NotebookNew(paHoriz->div[1], AG_NOTEBOOK_EXPAND);
	ntab = AG_NotebookAddTab(nb, _("Input Stream"), AG_BOX_VERT);
	vsp->gui.playerIn = VS_PlayerNew(ntab, VS_PLAYER_EXPAND, vIn);
	ntab = AG_NotebookAddTab(nb, _("Output Stream"), AG_BOX_VERT);
	vsp->gui.playerOut = VS_PlayerNew(ntab, VS_PLAYER_EXPAND, vOut);

	boxStatus = AG_BoxNewHoriz(win, AG_BOX_HFILL);
	{
		sb = AG_StatusbarNew(boxStatus, AG_STATUSBAR_HFILL);
		vsp->gui.status = AG_StatusbarAddLabel(sb, "OK");
	
		boxParams = AG_BoxNewVert(boxStatus, AG_BOX_VFILL);
		{
			AG_NumericalNewDblR(boxParams, 0, NULL,
			    _("Bend: "), &vsp->bendSpeed, 1.0, vsp->bendSpeedMax);
			AG_NumericalNewIntR(boxParams, 0, NULL,
			    _("FPS: "), &vsp->frameRate, 1, 60);
		}
		
		AG_SeparatorNewVert(boxStatus);

		lbl = AG_LabelNewPolled(boxStatus, 0,
		    "FPS=%i\n"
		    "Drift=%i\n",
		    &vsp->frameRate, &vOut->drift);
		AG_LabelSizeHint(lbl, 2, "<Drift: XXXXX>");
		
		AG_SeparatorNewVert(boxStatus);

		pb = AG_ProgressBarNew(boxStatus, AG_PROGRESS_BAR_HORIZ,
		    AG_PROGRESS_BAR_SHOW_PCT);
		AG_BindInt(pb, "value", &vsp->gui.progress.val);
		AG_BindInt(pb, "min", &vsp->gui.progress.min);
		AG_BindInt(pb, "max", &vsp->gui.progress.max);
		AG_ProgressBarSetWidth(pb, agTextFontHeight*2);
		AG_ProgressBarSetLength(pb, 200);
	}

	/*
	 * Main application menu
	 */
	m = AG_MenuNode(menu->root, _("File"), NULL);
	{
		AG_MenuAction(m, _("Load video stream..."), agIconLoad.s,
		    LoadVideoDlg, "%p", vIn);
		AG_MenuAction(m, _("Load audio stream..."), agIconLoad.s,
		    LoadAudioDlg, "%p", vOut);
		AG_MenuSeparator(m);
		AG_MenuAction(m, _("Save video as..."), agIconSave.s,
		    SaveVideoDlg, "%p", vOut);
	}
	m = AG_MenuNode(menu->root, _("Edit"), NULL);
	{
		AG_MenuUintFlagsMp(m, _("Key learn mode"), vsIconControls.s,
		    &vsp->flags, VS_PROJECT_LEARNING, 0, &OBJECT(vsp)->lock);
	}
	m = AG_MenuNode(menu->root, _("MIDI"), NULL);
	{
		VS_MidiDevicesMenu(vIn->midi, m, VS_MIDI_INPUT);
		VS_MidiDevicesMenu(vIn->midi, m, VS_MIDI_OUTPUT);
	}

	AG_PaneMoveDividerPct(paHoriz, 60);
	AG_WindowSetGeometryAlignedPct(win, AG_WINDOW_MC, 65, 50);
	return (win);
}

AG_ObjectClass vsProjectClass = {
	"VS_Project",
	sizeof(VS_Project),
	{ 0,0 },
	Init,
	NULL,			/* freeData */
	Destroy,
	Load,
	Save,
	Edit
};
