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

#include <vislak.h>
#include "vs_clip.h"

#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <errno.h>

VS_Clip *
VS_ClipNew(void)
{
	VS_Clip *vc;

	if ((vc = AG_TryMalloc(sizeof(VS_Clip))) == NULL) {
		return (NULL);
	}
	vc->frames = NULL;
	vc->n = 0;
	vc->dir = NULL;
	vc->fileFmt = NULL;

	vc->sndFile = NULL;
	vc->sndPos = 0;
	vc->sndBuf = NULL;
	vc->sndViz = NULL;
	vc->sndVizFrames = 0;
	vc->sndStream = NULL;
	vc->samplesPerFrame = 0;
	vc->midi = NULL;
	memset(&vc->sndInfo, 0, sizeof(vc->sndInfo));

	AG_MutexInitRecursive(&vc->lock);
	AG_MutexInitRecursive(&vc->sndLock);

	return (vc);
}

void
VS_ClipDestroy(VS_Clip *vc)
{
	AG_MutexDestroy(&vc->lock);
	AG_MutexDestroy(&vc->sndLock);
	Free(vc->frames);
	Free(vc->dir);
	Free(vc->fileFmt);
	Free(vc);
}

/* Set the archive file path. */
void
VS_ClipSetArchivePath(void *obj, const char *path)
{
	const char *c;

	AG_ObjectSetArchivePath(obj, path);

	if ((c = strrchr(path, PATHSEPC)) != NULL && c[1] != '\0') {
		AG_ObjectSetNameS(obj, &c[1]);
	} else {
		AG_ObjectSetNameS(obj, path);
	}
}

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

static int
LoadFrameJPEG(VS_Clip *vc, VS_Frame *vf, FILE *f)
{
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPROW pRow[1];
	AG_Surface *su = NULL;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		AG_SetError("Error loading JPEG image");
		return (-1);
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
		goto fail;

	/* Start decompression */
	(void)jpeg_start_decompress(&cinfo);
	
	while (cinfo.output_scanline < su->h) {
		pRow[0] = (JSAMPROW)(Uint8 *)su->pixels +
		          cinfo.output_scanline*su->pitch;
		jpeg_read_scanlines(&cinfo, pRow, (JDIMENSION)1);
	}

	/* Create the thumbnail. */
	AG_MutexLock(&vc->lock);
	if (AG_ScaleSurface(su, vsThumbSz, vsThumbSz, &vf->thumb) == -1) {
		AG_MutexUnlock(&vc->lock);
		goto fail;
	}
	AG_MutexUnlock(&vc->lock);

	AG_SurfaceFree(su);
	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return (0);
fail:
	if (su != NULL) { AG_SurfaceFree(su); }
	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return (-1);
}

/* Create a frame from image file */
int
VS_ClipAddFrame(VS_Clip *vc, const char *path)
{
	VS_Frame *framesNew, *vf;
	FILE *f;
	char *s;

	if ((f = fopen(path, "rb")) == NULL) {
		return (-1);
	}
	if ((framesNew = AG_TryRealloc(vc->frames, (vc->n+1)*sizeof(VS_Frame)))
	    == NULL) {
		goto fail;
	}

	AG_MutexLock(&vc->lock);
	vc->frames = framesNew;
	vf = &vc->frames[vc->n++];
	vf->thumb = NULL;
	vf->f = (vc->n-1);
	vf->flags = 0;
	vf->midiKey = -1;
	vf->kbdKey = 0;
	if ((s = strrchr(path, '.')) != NULL && s[1] != '\0') {
		if (!strcasecmp(&s[1], "jpg") ||
		    !strcasecmp(&s[1], "jpeg")) {
			if (LoadFrameJPEG(vc, vf, f) == -1)
				goto fail;
		}
	}
	AG_MutexUnlock(&vc->lock);

	fclose(f);
	return (0);
fail:
	fclose(f);
	return (-1);
}

/* Delete a range of frames. */
void
VS_ClipDelFrames(VS_Clip *vc, Uint f1, Uint f2)
{
	char pathOld[AG_PATHNAME_MAX];
	char pathNew[AG_PATHNAME_MAX];
	Uint i;

	printf("DELFRAMES(%u - %u)\n", f1, f2);

#if 0
	if (f2 == (vc->n-1)) {
		printf("Trivial: n->%u\n", (f2-f1));
		vc->n -= (f2-f1);
		return;
	}
#endif

	/* Delete frames f1 through f2. */
	for (i = f1; i < f2; i++) {
		VS_Frame *vf = &vc->frames[i];

		printf("Deleting: f%d\n", i);

		if (vf->thumb != NULL)
			AG_SurfaceFree(vf->thumb);
		if (vc->midi != NULL && vf->midiKey != -1)
			VS_MidiDelKey(vc->midi, vf->midiKey);

		VS_ClipGetFramePath(vc, i, pathOld, sizeof(pathOld));
		if (unlink(pathOld) == -1)
			fprintf(stderr, "%s: %s\n", pathOld, strerror(errno));
	}
	printf("Memmove: f%d <- f%d (n=%d)\n", f1, f2, f2-f1);
	memmove(&vc->frames[f1], &vc->frames[f2], (f2-f1)*sizeof(VS_Frame));

	/* Renumber the remaining frames. */
	for (i = f2; i < vc->n; i++) {
		VS_ClipGetFramePath(vc, i, pathOld, sizeof(pathOld));
		VS_ClipGetFramePath(vc, i - (f2-f1), pathNew, sizeof(pathNew));
		printf("Rename: %s -> %s\n", pathOld, pathNew);
		if (rename(pathOld, pathNew) == -1) {
			fprintf(stderr, "Rename %s -> %s: %s\n",
			    pathOld, pathNew,
			    strerror(errno));
		}
		vc->frames[i].f = i;
	}
	vc->n -= (f2-f1);
}

/* Append a frame from another clip into a clip. */
int
VS_ClipCopyFrame(VS_Clip *vcDst, VS_Clip *vcSrc, Uint f)
{
	VS_Frame *framesNew;
	VS_Frame *vfDst, *vfSrc;

	if (f >= vcSrc->n) {
		AG_SetError("No such frame: %u", f);
		return (-1);
	}
	vfSrc = &vcSrc->frames[f];

	if ((framesNew = AG_TryRealloc(vcDst->frames,
	    (vcDst->n+1)*sizeof(VS_Frame))) == NULL) {
		return (-1);
	}
	AG_MutexLock(&vcSrc->lock);
	AG_MutexLock(&vcDst->lock);
	vcDst->frames = framesNew;
	vfDst = &vcDst->frames[vcDst->n++];
	vfDst->thumb = (vfSrc->thumb) ? AG_SurfaceDup(vfSrc->thumb) : NULL;
	vfDst->f = (vcDst->n-1);
	vfDst->flags = 0;
	vfDst->midiKey = -1;
	AG_MutexUnlock(&vcDst->lock);
	AG_MutexUnlock(&vcSrc->lock);
	return (0);
}

/* Return full path to image file associated with a video frame. */
void
VS_ClipGetFramePath(VS_Clip *vc, Uint f, char *dst, size_t dstLen)
{
	char file[AG_FILENAME_MAX];

	snprintf(file, sizeof(file), vc->fileFmt, vsFileFirst+f);
	Strlcpy(dst, (vc->dir != NULL) ? vc->dir : vsInputDir, dstLen);
	Strlcat(dst, AG_PATHSEP, dstLen);
	Strlcat(dst, file, dstLen);
}
