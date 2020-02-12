/*
 * Copyright (c) 2010-2013 Hypertriton, Inc. <http://hypertriton.com/>
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

#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>

int vsPlayerLOD = 0;			/* Auto LOD adjustment (for slow hw) */
int vsPlayerButtonHeight = 20;

VS_Player *
VS_PlayerNew(void *parent, Uint flags, struct vs_clip *clip)
{
	VS_Player *vp;
	VS_Project *vsp = clip->proj;

	vp = Malloc(sizeof(VS_Player));
	AG_ObjectInit(vp, &vsPlayerClass);

	vp->flags |= flags;
	vp->clip = clip;

	AG_BindFlag(vp->btn[VS_PLAYER_PLAY], "state",
	    &vsp->flags, VS_PROJECT_PLAYING);
	AG_BindFlag(vp->btn[VS_PLAYER_REC], "state",
	    &vsp->flags, VS_PROJECT_RECORDING);

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
	
	AG_ObjectLock(vp);
	AG_ObjectLock(vp->clip->proj);

	vp->clip->x = 0;

	AG_ObjectUnlock(vp->clip->proj);
	AG_ObjectUnlock(vp);
}

static void
Forward(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);
	VS_Project *vsp;

	AG_ObjectLock(vp);
	vsp = vp->clip->proj;
	AG_ObjectLock(vsp);

	if (vp->clip->n > 0)
		vp->clip->x = (vp->clip->n - 1);

	AG_ObjectUnlock(vsp);
	AG_ObjectUnlock(vp);
}

void
VS_Stop(VS_Player *vp)
{
	VS_Clip *v;
	VS_Project *vsp;

	AG_ObjectLock(vp);
	v = vp->clip;
	vsp = v->proj;
	AG_ObjectLock(vsp);

	if (v->sndBuf != NULL) {
		if (VS_StopAudio(vp) == -1) {
			VS_Status(vsp, _("Failed to stop audio: %s"),
			    AG_GetError());
		} else {
			VS_Status(vsp, _("Audio/video playback stopped"));
		}
	} else {
		VS_Status(vsp, _("Video playback stopped"));
	}
	vsp->flags &= ~(VS_PROJECT_RECORDING);
	vsp->flags &= ~(VS_PROJECT_PLAYING);
	vp->flags &= ~(VS_PLAYER_PLAYING);
	
	AG_ObjectUnlock(vsp);
	AG_ObjectUnlock(vp);
}

void
VS_Play(VS_Player *vp)
{
	VS_Clip *v;
	VS_Project *vsp;

	AG_ObjectLock(vp);
	v = vp->clip;
	vsp = v->proj;
	AG_ObjectLock(vsp);

	VS_Stop(vsp->gui.playerIn);
	VS_Stop(vsp->gui.playerOut);

	vsp->flags &= ~(VS_PROJECT_RECORDING);
	vsp->flags &= ~(VS_PROJECT_PLAYING);

	if (v->sndBuf != NULL) {
		if (VS_PlayAudio(vp) == -1) {
			VS_Status(vsp, _("Failed to start audio: %s"),
			    AG_GetError());
			goto out;
		} else {
			VS_Status(vsp, _("Playing (%u frames, %d-Ch, %uHz)"),
			    v->n, v->sndInfo.channels, v->sndInfo.samplerate);
		}
	} else {
		VS_Status(vsp, _("Playing (%u frames, no sound)"), v->n);
	}
	vp->flags |= VS_PLAYER_PLAYING;
	vsp->flags |= VS_PROJECT_PLAYING;
out:
	AG_ObjectUnlock(vsp);
	AG_ObjectUnlock(vp);
}

static void
Play(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);

	VS_Play(vp);
}

static void
Stop(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);

	VS_Stop(vp);
}

static void
Record(AG_Event *event)
{
	VS_Player *vp = AG_PTR(1);
	VS_Project *vsp;

	AG_ObjectLock(vp);
	vsp = vp->clip->proj;
	AG_ObjectLock(vsp);
	
	if (vsp->flags & VS_PROJECT_RECORDING)
		goto out;

	VS_Stop(vsp->gui.playerIn);
	VS_Stop(vsp->gui.playerOut);

	/* TODO allow other tracks to be played */
	if (vsp->gui.playerOut->clip->sndBuf != NULL &&
	    VS_PlayAudio(vsp->gui.playerOut) == -1) {
		goto out;
	}
	vsp->flags |= VS_PROJECT_RECORDING;
out:	
	AG_ObjectUnlock(vsp);
	AG_ObjectUnlock(vp);
}

static void
Init(void *obj)
{
	VS_Player *vp = obj;
	int i;

	vp->clip = NULL;
	vp->wPre = 320;
	vp->hPre = 240 + vsPlayerButtonHeight;
	vp->xLast = -1;
	vp->suScaled = -1;

	vp->btn[VS_PLAYER_REW] = AG_ButtonNewFn(vp, 0, _("Rew"),
	    Rewind, "%p", vp);
	vp->btn[VS_PLAYER_PLAY] = AG_ButtonNewFn(vp, AG_BUTTON_STICKY, _("Play"),
	    Play, "%p", vp);
	vp->btn[VS_PLAYER_STOP] = AG_ButtonNewFn(vp, 0, _("Stop"),
	    Stop, "%p", vp);
	vp->btn[VS_PLAYER_FWD] = AG_ButtonNewFn(vp, 0, _("Fwd"),
	    Forward, "%p", vp);
	vp->btn[VS_PLAYER_REC] = AG_ButtonNewFn(vp, AG_BUTTON_STICKY, _("Rec"),
	    Record, "%p", vp);
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
	vp->rVid.h = MIN(a->w, a->h) - vsPlayerButtonHeight;
	vp->flags |= VS_PLAYER_REFRESH;

	aBtn.x = 1;
	aBtn.y = a->h - vsPlayerButtonHeight;
	aBtn.w = wBtn;
	aBtn.h = vsPlayerButtonHeight;

	for (i = 0; i < VS_PLAYER_LASTBTN; i++) {
		AG_WidgetSizeAlloc(vp->btn[i], &aBtn);
		aBtn.x += wBtn + 1;
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
DrawFromJPEG(VS_Player *vp, VS_Clip *v, VS_Frame *vf)
{
	char path[AG_PATHNAME_MAX];
	FILE *f;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPROW pRow[1];
	AG_Surface *su = NULL, *suScaled;

	snprintf(path, sizeof(path), v->fileFmt, v->dir, vf->f);
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
	jpeg_read_header(&cinfo, TRUE);

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
	jpeg_start_decompress(&cinfo);
	
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

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return;
fail_free:
	if (vp->suScaled != -1) {
		AG_WidgetUnmapSurface(vp, vp->suScaled);
		vp->suScaled = -1;
	}
	if (su != NULL) { AG_SurfaceFree(su); }
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}

static void
Draw(void *obj)
{
	VS_Player *vp = obj;
	VS_Clip *v = vp->clip;
	VS_Project *vsp = v->proj;
	int i;
	
	AG_ObjectLock(vsp);
	if (vsp->procOp != VS_PROC_IDLE ||
	    v->x >= v->n) {
		AG_Color c;

		AG_ColorBlack(&c);
		AG_DrawBox(vp, &vp->rVid, -1, &c);
		goto out;
	}
	if (vsPlayerLOD) {
		if (vp->xLast != v->x ||
		    vp->flags & VS_PLAYER_REFRESH) {
			vp->xLast = v->x;
			DrawFromThumb(vp, v->frames[v->x].thumb);
			vp->flags &= ~(VS_PLAYER_REFRESH|VS_PLAYER_LOD);
		} else {
			if (!(vp->flags & VS_PLAYER_LOD) &&
			    vp->lodTimeout++ > 5) {
				vp->lodTimeout = 0;
				vp->flags |= VS_PLAYER_LOD;
				DrawFromJPEG(vp, v, &v->frames[v->x]);
			}
		}
	} else {
		DrawFromJPEG(vp, v, &v->frames[v->x]);
	}

	AG_PushClipRect(vp, &vp->rVid);
	if (vp->suScaled != -1) {
		AG_WidgetBlitSurface(vp, vp->suScaled, 0, 0);
	}
	AG_PopClipRect(vp);
out:
	AG_ObjectUnlock(vsp);

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
	VS_Clip *v = vp->clip;
	float *out = (float *)pOut;
	int ch;
	Ulong i;

	for (i = 0; i < count; i++) {
		if (v->sndPos+1 >= v->sndInfo.frames*2) {
			*out++ = 0;
			*out++ = 0;
			continue;
		}
		*out++ = v->sndBuf[v->sndPos];
		*out++ = v->sndBuf[v->sndPos+1];
		v->sndPos+=2;
	}

	v->drift = v->sndPos - v->x*v->samplesPerFrame;
	if (v->drift > v->samplesPerFrame*2 ||
	    v->drift < v->samplesPerFrame*2) {
		v->sndPos = v->x*v->samplesPerFrame;
	}
	return (paContinue);
}

static int
AudioUpdateMono(const void *pIn, void *pOut, Ulong count,
    const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
    void *pData)
{
	VS_Player *vp = pData;
	VS_Clip *v = vp->clip;
	float *out = (float *)pOut;
	Ulong i;
	
	for (i = 0; i < count; i++) {
		if (v->sndPos+1 >= v->sndInfo.frames) {
			*out++ = 0;
			continue;
		}
		*out++ = v->sndBuf[v->sndPos];
		v->sndPos++;
	}

	v->drift = v->sndPos - v->x*v->samplesPerFrame;
	if (v->drift > v->samplesPerFrame*2 ||
	    v->drift < v->samplesPerFrame*2) {
		v->sndPos = v->x*v->samplesPerFrame;
	}
	return (paContinue);
}

#if 0
static void
VS_PlayerAudioFinishedCallback(void *pData)
{
	VS_Clip *v = pData;

	printf("audio finished!\n");
}
#endif

/* Start audio playback */
int
VS_PlayAudio(VS_Player *vp)
{
	VS_Clip *v = vp->clip;
	PaStreamParameters op;
	PaError rv;

	AG_MutexLock(&v->sndLock);
		
	if (v->sndBuf == NULL) {
		AG_SetError("Clip has no audio");
		goto fail;
	}
	if ((op.device = Pa_GetDefaultOutputDevice()) == -1) {
		AG_SetError("No audio output device");
		goto fail;
	}
	if (v->sndInfo.channels != 1 &&
	    v->sndInfo.channels != 2) {
		AG_SetError("%d-Ch playback unimplemented", v->sndInfo.channels);
		goto fail;
	}
	op.channelCount = v->sndInfo.channels;
	op.sampleFormat = paFloat32;
	op.suggestedLatency = Pa_GetDeviceInfo(op.device)->defaultLowOutputLatency;
	op.hostApiSpecificStreamInfo = NULL;

	rv = Pa_OpenStream(
	    &v->sndStream,
	    NULL,
	    &op,
	    v->sndInfo.samplerate,
	    v->samplesPerFrame,		/* frames per buffer */
	    paClipOff,
	    (v->sndInfo.channels == 2) ? AudioUpdateStereo : AudioUpdateMono,
	    vp);
	if (rv != paNoError) {
		AG_SetError("Failed to open playback device: %s",
		    Pa_GetErrorText(rv));
		goto fail;
	}

#if 0
	rv = Pa_SetStreamFinishedCallback(v->sndStream,
	    VS_PlayerAudioFinishedCallback);
	if (rv != paNoError)
		goto pafail;
#endif

	rv = Pa_StartStream(v->sndStream);
	if (rv != paNoError)
		goto pafail;

	AG_MutexUnlock(&v->sndLock);
	return (0);
pafail:
	AG_SetError("PortAudio error: %s", Pa_GetErrorText(rv));
fail:
	AG_MutexUnlock(&v->sndLock);
	return (-1);
}

int
VS_StopAudio(VS_Player *vp)
{
	VS_Clip *v = vp->clip;
	PaError rv;

	AG_MutexLock(&v->sndLock);

	if (v->sndStream == NULL) {
		AG_SetError("Audio is not playing");
		goto fail;
	}
	if ((rv = Pa_StopStream(v->sndStream)) != paNoError) {
		AG_SetError("%s", Pa_GetErrorText(rv));
		goto fail;
	}

	AG_MutexUnlock(&v->sndLock);
	return (0);
fail:
	AG_MutexUnlock(&v->sndLock);
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
