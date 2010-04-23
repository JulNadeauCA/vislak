/*	Public domain	*/

#ifndef _VISLAK_H_
#define _VISLAK_H_

#include <agar/core.h>
#include <agar/core/types.h>
#include <agar/gui.h>
#include <agar/math.h>

#ifdef _VS_INTERNAL

# ifndef MIN
#  define MIN(a,b) (((a)<(b))?(a):(b))
# endif
# ifndef MAX
#  define MAX(a,b) (((a)>(b))?(a):(b))
# endif

# include "config/enable_nls.h"
# ifdef ENABLE_NLS
#  include <libintl.h>
#  define _(String) gettext(String)
#  define gettext_noop(String) (String)
#  define N_(String) gettext_noop(String)
# else
#  undef _
#  undef N_
#  define _(s) (s)
#  define N_(s) (s)
# endif
# ifdef WIN32
#  define PATHSEPC '\\'
#  define PATHSEP "\\"
# else
#  define PATHSEPC '/'
#  define PATHSEP "/"
# endif
#endif /* _VS_INTERNAL */

#if !defined(__BEGIN_DECLS) || !defined(__END_DECLS)
# define _ES_DEFINED_CDECLS
# if defined(__cplusplus)
#  define __BEGIN_DECLS extern "C" {
#  define __END_DECLS   }
# else
#  define __BEGIN_DECLS
#  define __END_DECLS
# endif
#endif

typedef enum vs_proc_op {
	VS_PROC_INIT,			/* Initialization */
	VS_PROC_IDLE,			/* Idle */
	VS_PROC_LOAD_VIDEO,		/* Importing video data */
	VS_PROC_LOAD_AUDIO,		/* Importing audio data */
	VS_PROC_TERMINATE		/* Exiting */
} VS_ProcOp;

struct vs_player;

__BEGIN_DECLS
extern int vsThumbSz;			/* Thumbnail size in pixels */
extern int vsWaveSz;			/* Waveform size in pixels */
extern int pbVal, pbMin, pbMax;		/* For progress bar */
extern AG_Label *vsStatus;		/* Statusbar label */
extern VS_ProcOp vsProcOp;		/* Processing thread status */
extern AG_Mutex vsProcLock;		/* Lock on processing thread */
extern struct vs_player *vsPlaying;	/* Playing clip */
extern struct vs_player *vsPlayerIn;	/* Input stream preview */
extern struct vs_player *vsPlayerOut;	/* Output stream preview */
extern int vsRecording;			/* Recording output */
extern int vsLearning;
extern double vsBendSpeed;
extern double vsBendSpeedMax;
extern char vsInputDir[];
extern int vsFrameRate;
extern int vsFileFirst;

static __inline__ void
VS_SetProcessOp(VS_ProcOp op)
{
	AG_MutexLock(&vsProcLock);
	vsProcOp = op;
	AG_MutexUnlock(&vsProcLock);
}
__END_DECLS

#endif /* _VISLAK_H_ */
