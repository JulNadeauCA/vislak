/*
 * Copyright (c) 2010 Hypertriton, Inc. <http://hypertriton.com/>
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
 * Playback widget.
 */

#include <vislak.h>
#include "vs_player.h"
#include "vs_view.h"

#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>

int vsPlayerButtonHeight = 20;
int vsPlayerCompensation = 0;
int vsPlayerEnableComp = 0;

VS_Player *
VS_PlayerNew(void *parent, Uint flags, struct vs_view *vv)
{
	VS_Player *vp;

	vp = Malloc(sizeof(VS_Player));
	AG_ObjectInit(vp, &vsPlayerClass);

	vp->flags |= flags;
	vp->vv = vv;

	if (flags & VS_PLAYER_HFILL) { AG_ExpandHoriz(vp); }
	if (flags & VS_PLAYER_VFILL) { AG_ExpandVert(vp); }

	AG_ObjectAttach(parent, vp);
	return (vp);
}

void
VS_PlayerSizeHint(VS_Player *vp, Uint w, Uint h)
{
	vp->wPre = w;
	vp->hPre = h;
}

static void
Rewind(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);
	
	AG_ObjectLock(vp->vv);
	vp->vv->xOffs = 0;
	AG_ObjectUnlock(vp->vv);
}

void
VS_Stop(VS_Player *vp)
{
	if (vsPlayerOut != NULL &&
	    vsPlayerOut->vv->clip->sndBuf &&
	    VS_StopAudio(vsPlayerOut) == -1) {
		AG_LabelText(vsStatus, _("Failed to stop audio: %s"),
		    AG_GetError());
	} else {
		AG_LabelText(vsStatus, _("Playback stopped"));
	}
	vsPlaying = NULL;
	vsRecording = 0;
	AG_SetInt(vp->btn[VS_PLAYER_PLAY], "state", 0);
	AG_SetInt(vp->btn[VS_PLAYER_REC], "state", 0);
}

void
VS_Play(VS_Player *vp)
{
	VS_Clip *vc = vp->vv->clip;

	if (vsPlaying != NULL) {
		VS_Stop(vsPlaying);
		vsPlaying = NULL;
	}
	if (vsPlayerOut->vv->clip->sndBuf &&
	    VS_PlayAudio(vsPlayerOut) == -1) {
		AG_LabelText(vsStatus, _("Failed to start audio: %s"),
		    AG_GetError());
	}
	vsPlaying = vp;
	vsRecording = 0;
	AG_SetInt(vp->btn[VS_PLAYER_REC], "state", 0);

	if (vc->sndBuf) {
		AG_LabelText(vsStatus, _("Playing (%u frames, %d-Ch, %uHz)"),
		    vc->n, vc->sndInfo.channels, vc->sndInfo.samplerate);
	} else {
		AG_LabelText(vsStatus, _("Playing (%u frames, no sound)"),
		    vc->n);
	}
}

static void
Play(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);

	if (vsPlaying != vp) {
		VS_Play(vp);
	} else {
		VS_Stop(vp);
	}
}

static void
Stop(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);

	VS_Stop(vp);
}

static void
Forward(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);
	
	AG_ObjectLock(vp->vv);
	if (vp->vv->clip->n > 0) {
		vp->vv->xOffs = (vp->vv->clip->n - 1);
	}
	AG_ObjectUnlock(vp->vv);
}

static void
Record(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);

	if (!vsRecording) {
		if (vsPlayerOut->vv->clip->sndBuf &&
		    VS_PlayAudio(vsPlayerOut) == -1) {
			AG_TextMsgFromError();
			return;
		}
		vsPlaying = NULL;
		vsRecording = 1;
		AG_SetInt(vsPlayerIn->btn[VS_PLAYER_PLAY], "state", 0);
		AG_SetInt(vsPlayerOut->btn[VS_PLAYER_PLAY], "state", 0);
	} else {
		VS_Stop(vp);
	}
}

static void
Init(void *obj)
{
	VS_Player *vp = obj;
	int i;

	vp->vv = NULL;
	vp->wPre = 320;
	vp->hPre = 240 + vsPlayerButtonHeight;
	vp->xOffsLast = -1;
	vp->suScaled = -1;

	vp->btn[VS_PLAYER_REW] = AG_ButtonNewFn(vp, 0,
	    _("Rew"), Rewind, "%p", vp);
	vp->btn[VS_PLAYER_PLAY] = AG_ButtonNewFn(vp, AG_BUTTON_STICKY,
	    _("Play"), Play, "%p", vp);
	vp->btn[VS_PLAYER_STOP] = AG_ButtonNewFn(vp, 0,
	    _("Stop"), Stop, "%p", vp);
	vp->btn[VS_PLAYER_FWD] = AG_ButtonNewFn(vp, 0,
	    _("Fwd"), Forward, "%p", vp);
	vp->btn[VS_PLAYER_REC] = AG_ButtonNewFn(vp, AG_BUTTON_STICKY,
	    _("Rec"), Record, "%p", vp);
	
	for (i = 0; i < VS_PLAYER_SINE_SIZE; i++) {
		vp->sine[i] = (int) (sin(((double)i/(double)VS_PLAYER_SINE_SIZE)*M_PI*2.0)*10000.0);
	}
	vp->sinePhase = 0;
}

static void
SizeRequest(void *obj, AG_SizeReq *r)
{
	VS_Player *vp = obj;

	r->w = vp->wPre;
	r->h = vp->hPre;
}

static int
SizeAllocate(void *obj, const AG_SizeAlloc *a)
{
	VS_Player *vp = obj;
	AG_SizeAlloc aBtn;
	int wBtn = (a->w / VS_PLAYER_LASTBTN) - 1;
	int i;

	vp->rVid.x = 0;
	vp->rVid.y = 0;
	vp->rVid.w = a->w;
	vp->rVid.h = MIN(a->w, a->h);
	vp->flags |= VS_PLAYER_REFRESH;

	aBtn.x = 0;
	aBtn.y = a->h - vsPlayerButtonHeight;
	aBtn.w = wBtn;
	aBtn.h = vsPlayerButtonHeight;

	for (i = 0; i < VS_PLAYER_LASTBTN; i++) {
		AG_WidgetSizeAlloc(vp->btn[i], &aBtn);
		aBtn.x += wBtn;
	}
	return (0);
}

/*
 * Video rendering
 */

/* Update video from a frame thumbnail image. */
static void
DrawFromThumb(VS_Player *vp, AG_Surface *suSrc)
{
	AG_Surface *suScaled = NULL;

	/* XXX TODO: interlacing */
	if (AG_ScaleSurface(suSrc, vp->rVid.w, vp->rVid.h, &suScaled) == -1) {
		return;
	}
	if (vp->suScaled == -1) {
		vp->suScaled = AG_WidgetMapSurface(vp, suScaled);
	} else {
		AG_WidgetReplaceSurface(vp, vp->suScaled, suScaled);
	}
	return;
fail:
	if (vp->suScaled != -1) {
		AG_WidgetUnmapSurface(vp, vp->suScaled);
		vp->suScaled = -1;
	}
}

/*
 * Update video from image file.
 */
struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};
typedef struct my_error_mgr *my_error_ptr;
METHODDEF(void)
my_error_exit(j_common_ptr cinfo)
{
	my_error_ptr myerr = (my_error_ptr)cinfo->err;
	(*cinfo->err->output_message)(cinfo);
	longjmp(myerr->setjmp_buffer, 1);
}
static void
DrawFromJPEG(VS_Player *vp, VS_Clip *vc, VS_Frame *vf)
{
	char path[AG_PATHNAME_MAX];
	char file[AG_FILENAME_MAX];
	FILE *f;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPROW pRow[1];
	AG_Surface *su = NULL, *suScaled;

	snprintf(file, sizeof(file), vc->fileFmt, vf->f);
	Strlcpy(path, vsInputDir, sizeof(path));
	Strlcat(path, "/", sizeof(path));
	Strlcat(path, file, sizeof(path));
	if ((f = fopen(path, "r")) == NULL)
		return;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, f);
	(void)jpeg_read_header(&cinfo, TRUE);

	/* Allocate Agar surface */
	if (cinfo.num_components == 4) {
		cinfo.out_color_space = JCS_CMYK;
		cinfo.quantize_colors = FALSE;
		jpeg_calc_output_dimensions(&cinfo);

		su = AG_SurfaceRGBA(
		    cinfo.output_width,
		    cinfo.output_height,
		    32,
		    0,
#if AG_BYTEORDER == AG_BIG_ENDIAN
		    0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF
#else
		    0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
#endif
		);
	} else {
		cinfo.out_color_space = JCS_RGB;
		cinfo.quantize_colors = FALSE;
#if 1
		/* For speed */
		cinfo.scale_num   = 1;
		cinfo.scale_denom = 1;
		cinfo.dct_method = JDCT_FASTEST;
		cinfo.do_fancy_upsampling = FALSE;
#endif
		jpeg_calc_output_dimensions(&cinfo);
		su = AG_SurfaceRGB(
		    cinfo.output_width,
		    cinfo.output_height,
		    24,
		    0,
#if AG_BYTEORDER == AG_BIG_ENDIAN
		    0xff0000, 0x00ff00, 0x0000ff
#else
		    0x0000ff, 0x00ff00, 0xff0000
#endif
		);
	}
	if (su == NULL)
		goto fail_free;

	/* Start decompression */
	(void)jpeg_start_decompress(&cinfo);
	
	while (cinfo.output_scanline < su->h) {
		pRow[0] = (JSAMPROW)(Uint8 *)su->pixels +
		          cinfo.output_scanline*su->pitch;
		jpeg_read_scanlines(&cinfo, pRow, (JDIMENSION)1);
	}

	/*
	 * Scale to preview size.
	 * XXX TODO: interlacing
	 */
	suScaled = NULL;
	if (AG_ScaleSurface(su, vp->rVid.w, vp->rVid.h, &suScaled) == -1) {
		goto fail_free;
	}
	if (vp->suScaled == -1) {
		vp->suScaled = AG_WidgetMapSurface(vp, suScaled);
	} else {
		AG_WidgetReplaceSurface(vp, vp->suScaled, suScaled);
	}
	AG_SurfaceFree(su);

	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return;
fail_free:
	if (vp->suScaled != -1) {
		AG_WidgetUnmapSurface(vp, vp->suScaled);
		vp->suScaled = -1;
	}
	if (su != NULL) { AG_SurfaceFree(su); }
	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}

static void
Draw(void *obj)
{
	VS_Player *vp = obj;
	VS_View *vv = vp->vv;
	VS_Clip *vc;
	int i;
	
	if (vsProcOp != VS_PROC_IDLE)
		return;

	AG_ObjectLock(vv);

	if ((vc = vv->clip) == NULL ||
	    vv->xOffs < 0 || vv->xOffs >= vc->n) {
		goto out;
	}
	if (vp->xOffsLast != vv->xOffs ||
	    vp->flags & VS_PLAYER_REFRESH) {
		vp->xOffsLast = vv->xOffs;
		if (vv->xOffs < vv->clip->n) {
			DrawFromThumb(vp, vv->clip->frames[vv->xOffs].thumb);
		}
		vp->flags &= ~(VS_PLAYER_REFRESH|VS_PLAYER_LOD);
	} else {
		if (!(vp->flags & VS_PLAYER_LOD) &&
		    vp->lodTimeout++ > 5) {
			vp->lodTimeout = 0;
			vp->flags |= VS_PLAYER_LOD;
			DrawFromJPEG(vp, vv->clip, &vv->clip->frames[vv->xOffs]);
		}
	}
	AG_DrawBox(vp, vp->rVid, -1, AG_ColorRGB(0,0,0));
	AG_PushClipRect(vp, vp->rVid);
	if (vp->suScaled != -1) {
		AG_WidgetBlitSurface(vp, vp->suScaled, 0, 0);
	}
	AG_PopClipRect(vp);
out:
	AG_ObjectUnlock(vv);

	for (i = 0; i < VS_PLAYER_LASTBTN; i++)
		AG_WidgetDraw(vp->btn[i]);
}

/*
 * Audio Playback
 */

static int
AudioUpdateStereo(const void *pIn, void *pOut, Ulong count,
    const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
    void *pData)
{
	VS_Player *vp = pData;
	VS_Clip *vc = vp->vv->clip;
	float *out = (float *)pOut;
	int ch;
	Ulong i;

	for (i = 0; i < count; i++) {
		if (vc->sndPos+1 >= vc->sndInfo.frames*2) {
			*out++ = 0;
			*out++ = 0;
			continue;
		}
		*out++ = vc->sndBuf[vc->sndPos];
		*out++ = vc->sndBuf[vc->sndPos+1];
		vc->sndPos++;
	}

	//printf("diff = %d\n", vc->sndPos - vp->vv->xOffs*vc->samplesPerFrame);
	return (paContinue);
}

static int
AudioUpdateMono(const void *pIn, void *pOut, Ulong count,
    const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
    void *pData)
{
	VS_Player *vp = pData;
	VS_Clip *vc = vp->vv->clip;
	float *out = (float *)pOut;
	Ulong i;
	
	for (i = 0; i < count; i++) {
		if (vc->sndPos+1 >= vc->sndInfo.frames) {
			*out++ = 0;
			continue;
		}
		*out++ = vc->sndBuf[vc->sndPos];
		vc->sndPos++;
	}

	if (vsPlayerEnableComp) {
		vsPlayerCompensation = vc->sndPos - vp->vv->xOffs*vc->samplesPerFrame;
		if (vsPlayerCompensation > vc->samplesPerFrame*2 ||
		    vsPlayerCompensation < vc->samplesPerFrame*2) {
			vc->sndPos = vp->vv->xOffs*vc->samplesPerFrame;
		}
	}
	return (paContinue);
}

static void
VS_PlayerAudioFinishedCallback(void *pData)
{
	VS_Clip *vc = pData;

	printf("audio finished!\n");
}

/* Start audio playback */
int
VS_PlayAudio(VS_Player *vp)
{
	VS_Clip *vc = vp->vv->clip;
	PaStreamParameters op;
	PaError rv;

	AG_MutexLock(&vc->sndLock);
		
	if (vc->sndBuf == NULL) {
		AG_SetError("Clip has no audio");
		goto fail;
	}
	if ((op.device = Pa_GetDefaultOutputDevice()) == -1) {
		AG_SetError("No audio output device");
		goto fail;
	}
	if (vc->sndInfo.channels != 1 &&
	    vc->sndInfo.channels != 2) {
		AG_SetError("%d-Ch playback unimplemented", vc->sndInfo.channels);
		goto fail;
	}
	op.channelCount = vc->sndInfo.channels;
	op.sampleFormat = paFloat32;
	op.suggestedLatency = Pa_GetDeviceInfo(op.device)->defaultLowOutputLatency;
	op.hostApiSpecificStreamInfo = NULL;

	rv = Pa_OpenStream(
	    &vc->sndStream,
	    NULL,
	    &op,
	    vc->sndInfo.samplerate,
	    vc->samplesPerFrame,		/* frames per buffer */
	    paClipOff,
	    (vc->sndInfo.channels == 2) ? AudioUpdateStereo : AudioUpdateMono,
	    vp);
	if (rv != paNoError) {
		AG_SetError("Failed to open playback device: %s",
		    Pa_GetErrorText(rv));
		goto fail;
	}

	rv = Pa_SetStreamFinishedCallback(vc->sndStream,
	    VS_PlayerAudioFinishedCallback);
	if (rv != paNoError)
		goto pafail;

	rv = Pa_StartStream(vc->sndStream);
	if (rv != paNoError)
		goto pafail;

	AG_MutexUnlock(&vc->sndLock);
	return (0);
pafail:
	AG_SetError("PortAudio error: %s", Pa_GetErrorText(rv));
fail:
	AG_MutexUnlock(&vc->sndLock);
	return (-1);
}

int
VS_StopAudio(VS_Player *vp)
{
	VS_Clip *vc = vp->vv->clip;
	PaError rv;

	AG_MutexLock(&vc->sndLock);

	if (vc->sndStream == NULL) {
		AG_SetError("Audio is not playing");
		goto fail;
	}
	if ((rv = Pa_StopStream(vc->sndStream)) != paNoError) {
		AG_SetError("%s", Pa_GetErrorText(rv));
		goto fail;
	}

	AG_MutexUnlock(&vc->sndLock);
	return (0);
fail:
	AG_MutexUnlock(&vc->sndLock);
	return (0);
}

AG_WidgetClass vsPlayerClass = {
	{
		"AG_Widget:VS_Player",
		sizeof(VS_Player),
		{ 0,0 },
		Init,
		NULL,		/* free */
		NULL,		/* destroy */
		NULL,		/* load */
		NULL,		/* save */
		NULL		/* edit */
	},
	Draw,
	SizeRequest,
	SizeAllocate
};
