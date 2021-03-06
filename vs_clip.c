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

#include <vislak.h>

#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <errno.h>

VS_Clip *
VS_ClipNew(VS_Project *vsp)
{
	VS_Clip *v;
	int i;

	if ((v = AG_TryMalloc(sizeof(VS_Clip))) == NULL) {
		return (NULL);
	}
	v->proj = vsp;
	v->frames = NULL;
	v->n = 0;
	v->dir = NULL;
	v->audioFile = NULL;
	v->fileFmt = Strdup("%s/%08u.jpg");
	v->fileFirst = 1;
	v->fileLast = -1;

	v->x = 0;
	v->xVel = 0.0;
	v->xVelCur = 0.0;

	v->sndFile = NULL;
	v->sndPos = 0;
	v->sndBuf = NULL;
	v->sndViz = NULL;
	v->sndVizFrames = 0;
	v->sndStream = NULL;
	v->samplesPerFrame = 0;
	v->midi = NULL;
	memset(&v->sndInfo, 0, sizeof(v->sndInfo));

	for (i = 0; i < AG_KEY_LAST; i++) {
		v->kbdKeymap[i] = -1;
	}
	AG_MutexInitRecursive(&v->lock);
	AG_MutexInitRecursive(&v->sndLock);
	return (v);
}

/* Clear the KBD keymap */
Uint
VS_ClipClearKeys(VS_Clip *v)
{
	Uint i, nCleared = 0;
	
	for (i = 0; i < AG_KEY_LAST; i++) {
		if (v->kbdKeymap[i] != 0) {
			v->kbdKeymap[i] = 0;
			v->frames[i].kbdKey = -1;
			nCleared++;
		}
	}
	return (nCleared);
}

void
VS_ClipDestroy(VS_Clip *v)
{
	AG_MutexDestroy(&v->lock);
	AG_MutexDestroy(&v->sndLock);
	Free(v->frames);
	Free(v->dir);
	Free(v->audioFile);
	Free(v->fileFmt);
	Free(v);
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

/* Clip must be locked. */
static int
LoadFrameJPEG(VS_Clip *v, VS_Frame *vf, FILE *f)
{
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPROW pRow[1];
	AG_Surface *su = NULL;
	int thumbSz;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		AG_SetError("Error loading JPEG image");
		return (-1);
	}

	AG_MutexUnlock(&v->lock);

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

	(void)jpeg_start_decompress(&cinfo);
	
	AG_MutexLock(&v->lock);
	
	/* Read the image data. */
	while (cinfo.output_scanline < su->h) {
		pRow[0] = (JSAMPROW)(Uint8 *)su->pixels +
		          cinfo.output_scanline*su->pitch;
		jpeg_read_scanlines(&cinfo, pRow, (JDIMENSION)1);
	}

	/* Generate a thumbnail */
	if (AG_ScaleSurface(su, v->proj->thumbSz, v->proj->thumbSz, &vf->thumb)
	    == -1)
		goto fail;

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
VS_ClipAddFrame(VS_Clip *v, const char *path)
{
	VS_Frame *framesNew, *vf;
	FILE *f;
	char *s;

	if ((f = fopen(path, "rb")) == NULL) {
		return (-1);
	}
	AG_MutexLock(&v->lock);
	if ((framesNew = AG_TryRealloc(v->frames, (v->n + 1)*sizeof(VS_Frame)))
	    == NULL) {
		goto fail;
	}
	v->frames = framesNew;
	vf = &v->frames[v->n++];
	vf->thumb = NULL;
	vf->f = (v->n-1);
	vf->flags = 0;
	vf->midiKey = -1;
	vf->kbdKey = -1;
	if ((s = strrchr(path, '.')) != NULL && s[1] != '\0') {
		if (!strcasecmp(&s[1], "jpg") ||
		    !strcasecmp(&s[1], "jpeg")) {
			if (LoadFrameJPEG(v, vf, f) == -1)
				goto fail;
		}
	}
	AG_MutexUnlock(&v->lock);
	fclose(f);
	return (0);
fail:
	AG_MutexUnlock(&v->lock);
	fclose(f);
	return (-1);
}

/* Delete a range of frames. */
void
VS_ClipDelFrames(VS_Clip *v, Uint f1, Uint f2)
{
	char pathOld[AG_PATHNAME_MAX];
	char pathNew[AG_PATHNAME_MAX];
	Uint i;

	printf("DELFRAMES(%u - %u)\n", f1, f2);

#if 0
	if (f2 == (v->n-1)) {
		printf("Trivial: n->%u\n", (f2-f1));
		v->n -= (f2-f1);
		return;
	}
#endif

	/* Delete frames f1 through f2. */
	for (i = f1; i < f2; i++) {
		VS_Frame *vf = &v->frames[i];

		printf("Deleting: f%d\n", i);

		if (vf->thumb != NULL)
			AG_SurfaceFree(vf->thumb);
		if (v->midi != NULL && vf->midiKey != -1)
			VS_MidiDelKey(v->midi, vf->midiKey);
		if (vf->kbdKey != -1)
			v->kbdKeymap[vf->kbdKey] = -1;

		VS_ClipGetFramePath(v, i, pathOld, sizeof(pathOld));
		if (unlink(pathOld) == -1)
			fprintf(stderr, "%s: %s\n", pathOld, strerror(errno));
	}
	printf("Memmove: f%d <- f%d (n=%d)\n", f1, f2, f2-f1);
	memmove(&v->frames[f1], &v->frames[f2], (f2-f1)*sizeof(VS_Frame));

	/* Renumber the remaining frames. */
	for (i = f2; i < v->n; i++) {
		VS_ClipGetFramePath(v, i, pathOld, sizeof(pathOld));
		VS_ClipGetFramePath(v, i - (f2-f1), pathNew, sizeof(pathNew));
		printf("Rename: %s -> %s\n", pathOld, pathNew);
		if (rename(pathOld, pathNew) == -1) {
			fprintf(stderr, "Rename %s -> %s: %s\n",
			    pathOld, pathNew,
			    strerror(errno));
		}
		v->frames[i].f = i;
	}
	v->n -= (f2-f1);
}

/* Append a frame from another clip into a clip. */
int
VS_ClipCopyFrame(VS_Clip *vDst, VS_Clip *vSrc, Uint f)
{
	VS_Frame *framesNew;
	VS_Frame *vfDst, *vfSrc;

	if (f >= vSrc->n) {
		AG_SetError("No such frame: %u", f);
		return (-1);
	}
	vfSrc = &vSrc->frames[f];

	if ((framesNew = AG_TryRealloc(vDst->frames,
	    (vDst->n+1)*sizeof(VS_Frame))) == NULL) {
		return (-1);
	}
	AG_MutexLock(&vSrc->lock);
	AG_MutexLock(&vDst->lock);
	vDst->frames = framesNew;
	vfDst = &vDst->frames[vDst->n++];
	vfDst->thumb = (vfSrc->thumb) ? AG_SurfaceDup(vfSrc->thumb) : NULL;
	vfDst->f = (vDst->n-1);
	vfDst->flags = 0;
	vfDst->midiKey = -1;
	vfDst->kbdKey = -1;
	AG_MutexUnlock(&vDst->lock);
	AG_MutexUnlock(&vSrc->lock);
	return (0);
}

/* Return full path to image file associated with a video frame. */
void
VS_ClipGetFramePath(VS_Clip *v, Uint f, char *dst, size_t dstLen)
{
	char file[AG_FILENAME_MAX];

	snprintf(file, sizeof(file), v->fileFmt, v->fileFirst+f);
	Strlcpy(dst, v->dir, dstLen);
	Strlcat(dst, AG_PATHSEP, dstLen);
	Strlcat(dst, file, dstLen);
}
