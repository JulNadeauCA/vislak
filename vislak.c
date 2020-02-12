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

#include <agar/core.h>
#include <agar/gui.h>

#include "config/version.h"
#include "config/enable_nls.h"
#include "config/localedir.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sndfile.h>

#include "vislak.h"
#include "icons.h"

/* File extension mappings */
const AG_FileExtMapping vsFileExtMap[] = {
	{ ".vis",	"Vislak Project",	&vsProjectClass,	1 }
};
const Uint vsFileExtCount = sizeof(vsFileExtMap) / sizeof(vsFileExtMap[0]);

static void
Quit(void)
{
	AG_Terminate(0);
}

int
main(int argc, char *argv[])
{
	const char *fontSpec = NULL, *driverSpec = NULL;
	char *optArg = NULL, *ep;
	int optInd = 1, c, i, j;
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
	while ((c = AG_Getopt(argc, argv, "?hvd:t:", &optArg, &optInd))
	    != -1) {
		switch (c) {
		case 'v':
			printf("Vislak %s\n", VERSION);
			return (0);
		case 'd':
			driverSpec = optArg;
			break;
		case 't':
			fontSpec = optArg;
			break;
		case '?':
		default:
			printf("%s [-v] [-d agar-driver-spec] "
			       "[-t font,size,flags] [file ...]\n", agProgName);
			return (1);
		}
	}
	if (AG_InitGraphics(driverSpec) == -1) {
		fprintf(stderr, "%s\n", AG_GetError());
		return (1);
	}
	AG_ConfigLoad();

	if (fontSpec != NULL) {
		AG_TextParseFontSpec(fontSpec);
	}
#ifdef __APPLE__
	AG_BindGlobalKey(AG_KEY_Q,	AG_KEYMOD_META, Quit);
	AG_BindGlobalKey(AG_KEY_EQUALS,	AG_KEYMOD_META,	AG_ZoomIn);
	AG_BindGlobalKey(AG_KEY_MINUS,	AG_KEYMOD_META,	AG_ZoomOut);
	AG_BindGlobalKey(AG_KEY_0,	AG_KEYMOD_META,	AG_ZoomReset);
	AG_BindGlobalKey(AG_KEY_P,	AG_KEYMOD_META, AG_ViewCapture);
#else
	AG_BindGlobalKey(AG_KEY_ESCAPE, AG_KEYMOD_ANY,  Quit);
	AG_BindGlobalKey(AG_KEY_EQUALS,	AG_KEYMOD_CTRL,	AG_ZoomIn);
	AG_BindGlobalKey(AG_KEY_MINUS,	AG_KEYMOD_CTRL,	AG_ZoomOut);
	AG_BindGlobalKey(AG_KEY_0,	AG_KEYMOD_CTRL,	AG_ZoomReset);
	AG_BindGlobalKey(AG_KEY_F8,	AG_KEYMOD_ANY,  AG_ViewCapture);
#endif
	VS_InitGUI();
	AG_RegisterClass(&vsProjectClass);

	/* Initialize the audio subsystem. */
	if ((rv = Pa_Initialize()) != paNoError) {
		AG_SetError("PortAudio init failed: %s", Pa_GetErrorText(rv));
		Pa_Terminate();
		goto fail;
	}

	if (optInd < argc) {				/* File(s) to load */
		for (i = optInd; i < argc; i++) {
			const AG_FileExtMapping *me = NULL;
			AG_Event ev;
			char *ext;

			if ((ext = strrchr(argv[i], '.')) == NULL) {
				continue;
			}
			AG_EventInit(&ev);
			for (j = 0; j < vsFileExtCount; j++) {
				me = &vsFileExtMap[j];
				if (strcasecmp(ext, me->ext) == 0)
					break;
			}
			if (j == vsFileExtCount) {
				AG_Verbose("%s: unknown format; ignoring\n",
				    argv[i]);
				continue;
			}
			AG_EventPushPointer(&ev, "", me->cls);
			AG_EventPushString(&ev, "", argv[i]);
			VS_GUI_LoadObject(&ev);
		}
	} else if (optInd == argc) {
		VS_Project *vsp;

		vsp = VS_ProjectNew(&vsVfsRoot, _("New project"));
		if (vsp == NULL ||
		    VS_GUI_OpenObject(vsp) == NULL) {
			goto fail;
		}
	}

	AG_EventLoop();

	Pa_Terminate();
	VS_DestroyGUI();
	AG_DestroyGraphics();
	AG_Destroy();
	return (0);
fail:
	fprintf(stderr, "%s\n", AG_GetError());
	return (1);
}

