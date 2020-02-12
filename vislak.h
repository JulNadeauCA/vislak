/*	Public domain	*/

#ifndef _VISLAK_H_
#define _VISLAK_H_

#include <agar/core.h>
#include <agar/core/types.h>
#include <agar/gui.h>

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

#include "vs_clip.h"
#include "vs_player.h"
#include "vs_project.h"
#include "vs_view.h"
#include "vs_gui.h"

#endif /* _VISLAK_H_ */
