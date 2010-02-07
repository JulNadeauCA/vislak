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

/*
 * ViSlak (a la une vue su'l lac!)
 */

#include <agar/core.h>
#include <agar/gui.h>

#include "config/version.h"
#include "config/release.h"
#include "config/enable_nls.h"
#include "config/localedir.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sndfile.h>

#include "vislak.h"
#include "vs_view.h"
#include "vs_player.h"
#include "vs_midi.h"
#include "icons.h"
#include "icons_data.h"

char vsInputAudio[AG_PATHNAME_MAX];		/* Input audio file */
char vsInputDir[AG_PATHNAME_MAX];		/* Input video directory */

char *vsOutputDir = NULL;			/* Output directory */
char *vsFileFormat = "%.08u.jpg";		/* Filename format */
int   vsFileFirst = 1;				/* First frame to load */
int   vsFileLast = -1;				/* Last frame to load (or -1) */
int   vsThumbSz = 128;				/* Thumbnail size in pixels */
int   vsWaveSz = 64;				/* Waveform size in pixels */
int   Rflag = 0;				/* Load files randomly */
int   vsFrameRate = 30;				/* Output frame rate */

AG_Mutex vsProcLock;				/* Lock on processing thread */
VS_ProcOp vsProcOp = VS_PROC_INIT;		/* Processing thread status */
int       vsRecording = 0;			/* Recording in progress */
VS_Player *vsPlaying = NULL;			/* Playback in progress */
int       vsLearning = 0;			/* Learning MIDI keys */
double    vsBendSpeed = 10.0;
double    vsBendSpeedMax = 50.0;

AG_Thread thProc;				/* Main processing thread */
AG_Window *wMain;				/* Main GUI window */
VS_Player *vsPlayerIn = NULL;			/* Input stream preview */
VS_Player *vsPlayerOut = NULL;			/* Output stream preview */
VS_Clip *vcInput = NULL;			/* Clip for input frames */
VS_Clip *vcOutput = NULL;			/* Clip for output */
VS_View *vvInput;				/* To display vcInput */
VS_View *vvOutput;				/* To display vcOutput */
int pbVal = 0, pbMin = 0, pbMax = 0;		/* For progress bar */
int vsCurFrame = 0;				/* Current frame */
AG_Window *wLoading;
AG_Label *vsStatus = NULL;			/* Statusbar label */
AG_Menu *vsMenu;				/* Main menu */

static void
printusage(void)
{
	printf("%s [-v] [-o output-dir] [-a audio-file] [-s thumb-size] "
	       " [-r fps] [-R first[-last]] [-f filename-fmt]"
	       " [-d agar-driverspec]"
	       " [-t font,size,flags]"
	       " [input-dir]\n", agProgName);
}

/* Load audio stream. */
static int
LoadAudio(VS_Clip *vc)
{
	sf_count_t nReadFrames = 0;
	sf_count_t i, j, frames_px, pos;
	int tri = 0, tflag = 0;

	if (vc->sndFile != NULL) {
		sf_close(vc->sndFile);
		Free(vc->sndBuf);
		Free(vc->sndViz);
		vc->sndBuf = NULL;
		vc->sndViz = NULL;
		vc->sndVizFrames = 0;
	}

	/*
	 * Read the raw audio data.
	 */
	memset(&vc->sndInfo, 0, sizeof(vc->sndInfo));
	vc->sndPos = 0;
	vc->sndFile = sf_open(vsInputAudio, SFM_READ, &vc->sndInfo);
	if (vc->sndFile == NULL) {
		AG_SetError("Cannot open %s", vsInputAudio);
		return (-1);
	}
	if ((vc->sndBuf = AG_TryMalloc(vc->sndInfo.frames*vc->sndInfo.channels*
	                               sizeof(float))) == NULL) {
		goto fail;
	}
	nReadFrames = 0;
	while (nReadFrames < vc->sndInfo.frames) {
		sf_count_t rv;

		rv = sf_readf_float(vc->sndFile, &vc->sndBuf[nReadFrames], 4096);
		if (rv == 0) {
			break;
		}
		nReadFrames += rv;
	}

	/* Compute the approximate number of audio samples per video frame */
	vc->samplesPerFrame = vc->sndInfo.samplerate/vsFrameRate;

	/*
	 * Compute a reduced waveform for visualization purposes.
	 */
	if (sf_command(vc->sndFile, SFC_CALC_SIGNAL_MAX, &vc->sndPeakSignal,
	    sizeof(vc->sndPeakSignal)) != 0) {
		vc->sndPeakSignal = 20000.0;
	}
	frames_px = vc->samplesPerFrame/vsThumbSz;
	vc->sndVizFrames = vc->sndInfo.frames/frames_px;
	if ((vc->sndViz = AG_TryMalloc(vc->sndVizFrames*sizeof(float))) == NULL) {
		goto fail;
	}
	for (i = 0; i < vc->sndVizFrames; i++) {
		vc->sndViz[i] = 0.0;
	}
	vc->sndPeakSignal /= 10000.0;
	for (i = 0, pos = 0;
	     i < vc->sndVizFrames && (pos + frames_px*vc->sndInfo.channels) < nReadFrames;
	     i++) {
		for (j = 0; j < frames_px*vc->sndInfo.channels; j++) {
			vc->sndViz[i] += MAX(vc->sndViz[i],
			    fabs(vc->sndBuf[pos++]/vc->sndPeakSignal));
		}
		vc->sndViz[i] /= frames_px*vc->sndInfo.channels;
	}

	AG_LabelText(vsStatus,
	    _("Audio import successful (%d-Ch, %dHz, %lu frames)"),
	    vc->sndInfo.channels,
	    vc->sndInfo.samplerate,
	    (Ulong)nReadFrames);
	return (0);
fail:
	sf_close(vc->sndFile);
	vc->sndFile = NULL;
	Free(vc->sndBuf);
	Free(vc->sndViz);
	return (-1);
}

/* Load frames in directory (without sorting). */
static int
LoadVideoRandom(VS_Clip *vc)
{
	char path[AG_PATHNAME_MAX];
	AG_Dir *d;
	AG_FileInfo fi;
	int i, nFrame = 1;

	if ((d = AG_OpenDir(vsInputDir)) == NULL) {
		return (-1);
	}
	pbVal = 0;
	pbMax = d->nents;
	for (i = 0; i < d->nents; i++) {
		char *filename = d->ents[i];

		if (filename[0] == '.') {
			pbVal++;
			continue;
		}
		Strlcpy(path, vsInputDir, sizeof(path));
		Strlcat(path, AG_PATHSEP, sizeof(path));
		Strlcat(path, filename, sizeof(path));
		if (AG_GetFileInfo(path, &fi) == -1) {
			pbVal++;
			continue;
		}
		if (fi.type == AG_FILE_DIRECTORY) {
			pbVal++;
			continue;
		}
		AG_LabelText(vsStatus, _("Importing: %s"), filename);
		if (VS_ClipAddFrame(vc, path, nFrame++) == -1) {
			goto fail;
		}
		pbVal++;
	}
	AG_CloseDir(d);
	return (0);
fail:
	AG_CloseDir(d);
	return (-1);
}

/* Load frames in directory (sorted per filename). */
static int
LoadVideoSorted(VS_Clip *vc)
{
	char path[AG_PATHNAME_MAX], file[AG_FILENAME_MAX];
	AG_Dir *d;
	Uint i;
	int fileOk;

	if ((d = AG_OpenDir(vsInputDir)) == NULL) {
		return (-1);
	}
	pbVal = vsFileFirst;
	pbMax = (vsFileLast != -1) ? vsFileLast : d->nents;
	AG_CloseDir(d);

	i = vsFileFirst;
	do {
		snprintf(file, sizeof(file), vc->fileFmt, i);
		Strlcpy(path, vsInputDir, sizeof(path));
		Strlcat(path, AG_PATHSEP, sizeof(path));
		Strlcat(path, file, sizeof(path));

		if (AG_FileExists(path) == -1) {
			fileOk = 0;
		} else {
			AG_LabelText(vsStatus, _("Importing: %s"), file);
			fileOk = (VS_ClipAddFrame(vc, path, i) == 0) ? 1 : 0;
		}
		pbVal++;
		i++;
	} while (fileOk && (vsFileLast == -1 || i < vsFileLast));

	pbVal = pbMax;
	return (0);
}

/* Import frames from disk. */
static int
LoadVideo(VS_Clip *vc)
{
	int rv;

	pbMin = 0;
	pbVal = 0;
	pbMax = 0;
	if (Rflag) {
		rv = LoadVideoRandom(vc);
	} else {
		rv = LoadVideoSorted(vc);
	}
	if (rv == 0) {
		AG_LabelText(vsStatus, _("Video import successful (%u frames)"),
		    vc->n);
	}
	return (rv);
}

/*
 * Main processing thread. This thread updates the output clip when
 * recording, and also handles slow operations such as imports.
 */
static void *
ProcessThread(void *ptr)
{
	char pathIn[AG_PATHNAME_MAX];
	char pathOut[AG_PATHNAME_MAX];
	Uint32 t1, t2 = 0;
	int rCur = 0;
	int rNom = (1000/vsFrameRate);
	int rIdleThresh = 10;
	int delta;
	Uint fIn;

	t1 = AG_GetTicks();
	for (;;) {
		t2 = AG_GetTicks();

		if (vsProcOp == VS_PROC_IDLE &&
		    t2-t1 >= rNom) {
			AG_MutexLock(&vsProcLock);

			if (vsRecording) {
				AG_MutexLock(&vcInput->lock);
				AG_MutexLock(&vcOutput->lock);
				fIn = vvInput->xOffs+1;
				if (VS_ClipCopyFrame(vcOutput, vcInput, fIn)
				    == -1) {
					AG_LabelText(vsStatus,
					    _("Interrupted: %s"),
					    AG_GetError());
					vsRecording = 0;
				}

				snprintf(pathIn, sizeof(pathIn),
				    "%s/%08u.jpg", vsInputDir, fIn);
				snprintf(pathOut, sizeof(pathOut),
				    "%s/%08u.jpg", vsOutputDir, vcOutput->n-1);
relink:
				if (link(pathIn, pathOut) == -1) {
					if (errno == EEXIST) {
						unlink(pathOut);
						goto relink;
					}
					AG_LabelText(vsStatus,
					    _("Interrupted: link(%s->%s): %s"),
					    pathIn, pathOut, strerror(errno));
					vsRecording = 0;
				}
				AG_MutexUnlock(&vcOutput->lock);
				AG_MutexUnlock(&vcInput->lock);
			}

			if (vvInput->xVel < -1.0 ||
			    vvInput->xVel > +1.0) {		/* >=1 frame */
				delta = (int)vvInput->xVel;
				if ((vvInput->xOffs+delta) >= 0 &&
				    (vvInput->xOffs+delta) < vcInput->n) {
					vvInput->xOffs += (int)vvInput->xVel;
				}
			} else if (vvInput->xVel != 0.0) {	/* Sub-frame */
				vvInput->xVelCur += vvInput->xVel;
				if (vvInput->xVelCur <= -1.0 ||
				    vvInput->xVelCur >= 1.0) {
					delta = (vvInput->xVelCur < 0) ? -1 : 1;
					vvInput->xVelCur = 0.0;
					if ((vvInput->xOffs+delta) >= 0 &&
					    (vvInput->xOffs+delta) < vcInput->n)
						vvInput->xOffs += delta;
				}
			}
	
			if (vsPlaying != NULL) {
				if (vsPlaying->vv->xOffs+1 >=
				    vsPlaying->vv->clip->n) {
					VS_Stop(vsPlaying);
				} else {
					vsPlaying->vv->xOffs++;
				}
			}
			if (vsRecording) {
				if (vcOutput->n > 1)
					vvOutput->xOffs++;
			}

			/* Update the effective refresh rate */
			t1 = AG_GetTicks();
			rCur = rNom - (t1-t2);
			if (rCur < 1) { rCur = 1; }

			AG_MutexUnlock(&vsProcLock);
		} else {
			AG_MutexLock(&vsProcLock);
			switch (vsProcOp) {
			case VS_PROC_LOAD_VIDEO:
				AG_MutexUnlock(&vsProcLock);
				if (LoadVideo(vcInput) == -1) {
					AG_LabelText(vsStatus,
					    _("Video import failed: %s"),
					    AG_GetError());
				}
				AG_MutexLock(&vsProcLock);
				vsProcOp = (vsInputAudio[0] != '\0') ?
				           VS_PROC_LOAD_AUDIO :
					   VS_PROC_IDLE;
				break;
			case VS_PROC_LOAD_AUDIO:
				AG_MutexUnlock(&vsProcLock);
				if (LoadAudio(vcOutput) == -1) {
					AG_LabelText(vsStatus,
					    _("Audio import failed: %s"),
					    AG_GetError());
				}
				AG_MutexLock(&vsProcLock);
				vsProcOp = VS_PROC_IDLE;
				break;
			case VS_PROC_TERMINATE:
				AG_LabelText(vsStatus, _("Terminating"));
				AG_MutexUnlock(&vsProcLock);
				goto quit;
			default:
				if (rCur > rIdleThresh) {
					AG_MutexUnlock(&vsProcLock);
					AG_Delay(rCur - rIdleThresh);
					AG_MutexLock(&vsProcLock);
				}
				break;
			}
			AG_MutexUnlock(&vsProcLock);
		}
	}
quit:
	AG_ThreadExit(NULL);
}

static void
VS_Quit(AG_Event *event)
{
	VS_SetProcessOp(VS_PROC_TERMINATE);
	AG_Delay(100);

	AG_QuitGUI();
}

/*
 * Load video stream.
 */
static void
LoadVideoJPEG(AG_Event *event)
{
	VS_Clip *vc = AG_PTR(1);
	char *path = AG_STRING(2);
	char *s;

	Strlcpy(vsInputDir, path, sizeof(vsInputDir));
	if ((s = strrchr(vsInputDir, PATHSEPC)) != NULL) {
		*s = '\0';
	}
	VS_SetProcessOp(VS_PROC_LOAD_VIDEO);
}
static void
LoadVideoDlg(AG_Event *event)
{
	VS_Clip *vc = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Import video..."));
	fd = AG_FileDlgNewMRU(win, "vislak.mru.video",
	    AG_FILEDLG_LOAD|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);

	AG_FileDlgAddType(fd, _("JPEG (select first frame)"), "*.jpg,*.jpeg",
	    LoadVideoJPEG, "%p", vc);

	AG_WindowShow(win);
}

/*
 * Load audio stream.
 */
static void
LoadAudioFile(AG_Event *event)
{
	VS_Clip *vc = AG_PTR(1);
	char *path = AG_STRING(2);

	Strlcpy(vsInputAudio, path, sizeof(vsInputAudio));
	VS_SetProcessOp(VS_PROC_LOAD_AUDIO);
}
static void
LoadAudioDlg(AG_Event *event)
{
	char extn[64];
	VS_Clip *vc = AG_PTR(1);
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
		    LoadAudioFile, "%p", vc);
	
		/* Map extra extensions */
		if (strcmp("OGG (OGG Container format)", fmt.name) == 0) {
			AG_FileDlgAddType(fd, fmt.name, ".ogg",
			    LoadAudioFile, "%p", vc);
		}
	}
	AG_WindowShow(win);
}

static void
SaveToMPEG4(AG_Event *event)
{
//	VS_Clip *vc = AG_PTR(1);
//	char *path = AG_STRING(2);

	/* TODO */
	/* VS_ClipSetArchivePath(vc, path); */
}

static void
SaveVideoDlg(AG_Event *event)
{
	VS_Clip *vc = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Export video as..."));
	fd = AG_FileDlgNewMRU(win, "vislak.mru.videos",
	    AG_FILEDLG_SAVE|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);
	AG_FileDlgSetOptionContainer(fd, AG_BoxNewVert(win, AG_BOX_HFILL));

	AG_FileDlgAddType(fd, _("MPEG-4"), "*.mpeg", SaveToMPEG4, "%p", vc);

	AG_WindowShow(win);
}

int
main(int argc, char *argv[])
{
	const char *fontSpec = NULL, *driverSpec = NULL;
	char *optArg = NULL, *ep;
	int optInd = 1;
	int c;
	AG_Pane *pa, *paHoriz;
	AG_MenuItem *m, *mNode;
	AG_FileInfo fi;
	PaError rv;

#ifdef ENABLE_NLS
	bindtextdomain("vislak", LOCALEDIR);
	bind_textdomain_codeset("vislak", "UTF-8");
	textdomain("vislak");
#endif
	if (AG_InitCore("vislak", AG_CREATE_DATADIR) == -1) {
		fprintf(stderr, "%s\n", AG_GetError());
		return (1);
	}
	vsInputAudio[0] = '\0';
	while ((c = AG_Getopt(argc, argv, "?hvf:r:s:o:d:t:a:", &optArg, &optInd))
	    != -1) {
		switch (c) {
		case 'v':
			printf("ViSlak %s (%s)\n", VERSION, RELEASE);
			exit(0);
		case 'f':
			vsFileFormat = optArg;
			break;
		case 'r':
			vsFrameRate = atoi(optArg);
			break;
		case 'R':
			vsFileFirst = (Uint)strtoul(optarg, &ep, 10);
			if (ep == optarg) {
				fprintf(stderr, "Invalid range\n");
				goto usage;
			}
			if (ep[0] == '-' && ep[1] != '\0') {
				vsFileLast = (Uint)strtoul(&ep[1], NULL, 10);
			}
			vsFileFormat = optArg;
			break;
		case 's':
			vsThumbSz = (int)strtoul(optarg, NULL, 10);
			break;
		case 'o':
			vsOutputDir = optArg;
			break;
		case 'd':
			driverSpec = optArg;
			break;
		case 't':
			fontSpec = optArg;
			break;
		case 'a':
			Strlcpy(vsInputAudio, optArg, sizeof(vsInputAudio));
			break;
		case '?':
		default:
			goto usage;
		}
	}
	if (vsOutputDir == NULL) {
		fprintf(stderr, "No output directory (-o) specified\n");
		goto usage;
	}
	if (AG_GetFileInfo(vsOutputDir, &fi) == 0) {
		if (fi.type == AG_FILE_REGULAR ||
		    fi.type == AG_FILE_DEVICE) {
			fprintf(stderr, "%s: not a directory\n", vsOutputDir);
			goto usage;
		}
	} else {
		if (AG_MkDir(vsOutputDir) == -1) {
			fprintf(stderr, "mkdir %s: %s\n", vsOutputDir,
			    AG_GetError());
			goto usage;
		}
	}
	if (vsInputAudio[0] != '\0' && !AG_FileExists(vsInputAudio)) {
		fprintf(stderr, "%s: No such file\n", vsInputAudio);
		goto usage;
	}
	if (argc-optInd == 1) {
		Strlcpy(vsInputDir, argv[optInd], sizeof(vsInputDir));
	} else if (argc-optInd > 1) {
		goto usage;
	} else {
		vsInputDir[0] = '\0';
	}
	if (fontSpec != NULL) {
		AG_TextParseFontSpec(fontSpec);
	}
	if (AG_InitGraphics(driverSpec) == -1) {
		fprintf(stderr, "%s\n", AG_GetError());
		return (1);
	}
	AG_BindGlobalKey(AG_KEY_ESCAPE, AG_KEYMOD_ANY, AG_QuitGUI);
	AG_BindGlobalKey(AG_KEY_F8, AG_KEYMOD_ANY, AG_ViewCapture);
	
	AG_RegisterClass(&vsViewClass);
	AG_RegisterClass(&vsPlayerClass);
	vsIcon_Init();

	/* Initialize the audio subsystem. */
	if ((rv = Pa_Initialize()) != paNoError) {
		AG_SetError("PortAudio init failed: %s", Pa_GetErrorText(rv));
		Pa_Terminate();
		goto fail;
	}

	/* Create the input and output clips. */
	if ((vcInput = VS_ClipNew()) == NULL ||
	    (vcOutput = VS_ClipNew()) == NULL) {
		goto fail;
	}
	if ((vsInputDir[0] != '\0') &&
	    (vcInput->dir = strdup(vsInputDir)) == NULL) {
		goto fail;
	} else {
		vcInput->dir = NULL;
	}
	if ((vcInput->fileFmt = strdup(vsFileFormat)) == NULL ||
	    (vcOutput->dir = strdup(vsOutputDir)) == NULL ||
	    (vcOutput->fileFmt = strdup(vsFileFormat)) == NULL)
		goto fail;

	/*
	 * Main GUI window
	 */
	wMain = AG_WindowNew(agDriverSw ? AG_WINDOW_PLAIN : 0);
	vsMenu = AG_MenuNew(wMain, AG_MENU_HFILL);
	AG_WindowSetCaptionS(wMain, "ViSlak");
	{
		AG_Box *boxStatus;
		AG_Statusbar *sb;
		AG_ProgressBar *pb;
		AG_Numerical *num;
		AG_Notebook *nb;
		AG_NotebookTab *ntab;
		AG_Label *lbl;

		paHoriz = AG_PaneNewHoriz(wMain, AG_PANE_EXPAND);
		pa = AG_PaneNewVert(paHoriz->div[0], AG_PANE_EXPAND);
		{
			AG_LabelNewPolled(pa->div[0], AG_LABEL_HFILL,
			    _("Input (%u frames):"), &vcInput->n);
			vvInput = VS_ViewNew(pa->div[0],
			    VS_VIEW_NOAUDIO|VS_VIEW_EXPAND,
			    vcInput);
		}
		{
			AG_LabelNewPolled(pa->div[0], AG_LABEL_HFILL,
			    _("Output (%u frames):"), &vcOutput->n);
			vvOutput = VS_ViewNew(pa->div[1],
			    VS_VIEW_EXPAND,
			    vcOutput);
		}
		
		nb = AG_NotebookNew(paHoriz->div[1], AG_NOTEBOOK_EXPAND);
		ntab = AG_NotebookAddTab(nb, _("Input Stream"), AG_BOX_VERT);
		vsPlayerIn = VS_PlayerNew(ntab, VS_PLAYER_EXPAND, vvInput);
		ntab = AG_NotebookAddTab(nb, _("Output Stream"), AG_BOX_VERT);
		vsPlayerOut = VS_PlayerNew(ntab, VS_PLAYER_EXPAND, vvOutput);

		boxStatus = AG_BoxNewHoriz(wMain, AG_BOX_HFILL);
		{
			sb = AG_StatusbarNew(boxStatus, AG_STATUSBAR_HFILL);
			vsStatus = AG_StatusbarAddLabel(sb, AG_LABEL_STATIC, "OK");

			num = AG_NumericalNewDblR(boxStatus, 0, NULL,
			    _("Bend: "), &vsBendSpeed, 1.0, vsBendSpeedMax);

			lbl = AG_LabelNewPolled(boxStatus, 0, "Comp: %i",
			    &vsPlayerCompensation);
			AG_LabelSizeHint(lbl, 1, "<Comp: XXXXX>");

			pb = AG_ProgressBarNew(boxStatus,
			    AG_PROGRESS_BAR_HORIZ, AG_PROGRESS_BAR_SHOW_PCT);
			AG_BindInt(pb, "value", &pbVal);
			AG_BindInt(pb, "min", &pbMin);
			AG_BindInt(pb, "max", &pbMax);
			AG_ProgressBarSetWidth(pb, agTextFontHeight*2);
			AG_ProgressBarSetLength(pb, 200);
		}
	}
	AG_WindowSetGeometryAlignedPct(wMain, AG_WINDOW_MC, 65, 50);
	if (agDriverSw) {
		AG_WindowMaximize(wMain);
	}
	AG_WindowShow(wMain);

	/*
	 * Main application menu
	 */
	m = AG_MenuAddItem(vsMenu, _("File"));
	{
		AG_MenuAction(m, _("Load video stream..."), agIconLoad.s,
		    LoadVideoDlg, "%p", vcInput);
		AG_MenuAction(m, _("Load audio stream..."), agIconLoad.s,
		    LoadAudioDlg, "%p", vcOutput);
		AG_MenuSeparator(m);
		AG_MenuAction(m, _("Save video as..."), agIconSave.s,
		    SaveVideoDlg, "%p", vcOutput);
		AG_MenuSeparator(m);
		AG_MenuActionKb(m, _("Quit"), agIconClose.s,
		    AG_KEY_Q, AG_KEYMOD_CTRL,
		    VS_Quit, NULL);
	}
	m = AG_MenuAddItem(vsMenu, _("Edit"));
	{
		AG_MenuBoolMp(m, _("MIDI learn mode"), vsIconControls.s, /* XXX icon */
		    &vsLearning, 0, &vsProcLock);
	}
	m = AG_MenuAddItem(vsMenu, _("MIDI"));
	{
		mNode = AG_MenuNode(m, _("MIDI Input"), vsIconControls.s);
		VS_MidiDevicesMenu(vvInput->midi, mNode, VS_MIDI_INPUT);
		mNode = AG_MenuNode(m, _("MIDI Output"), vsIconControls.s);
		VS_MidiDevicesMenu(vvInput->midi, mNode, VS_MIDI_OUTPUT);
	}

	/* Spawn the processing thread. */
	AG_MutexInitRecursive(&vsProcLock);
	AG_ThreadCreate(&thProc, ProcessThread, NULL);

	if (vsInputDir[0] != '\0') {
		/*
		 * Initiate the video import. If [-a] was passed, the audio
		 * import will follow.
		 */
		VS_SetProcessOp(VS_PROC_LOAD_VIDEO);
	}

	AG_EventLoop();
	AG_Destroy();
	Pa_Terminate();
	return (0);
fail:
	if (vcInput != NULL) { VS_ClipDestroy(vcInput); }
	if (vcOutput != NULL) { VS_ClipDestroy(vcOutput); }
	fprintf(stderr, "%s\n", AG_GetError());
	return (1);
usage:
	printusage();
	return (1);
}

