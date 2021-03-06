/*	Public domain	*/

#ifndef _VISLAK_VIEW_H_
#define _VISLAK_VIEW_H_

#include <agar/gui/widget.h>
#include <agar/gui/scrollbar.h>

typedef struct vs_view {
	struct ag_widget wid;

	Uint flags;
#define VS_VIEW_HFILL	0x01
#define VS_VIEW_VFILL	0x02
#define VS_VIEW_PANNING	0x10	/* Panning in progress */
#define VS_VIEW_NOAUDIO	0x20	/* No audio waveform display */
#define VS_VIEW_NOMIDI	0x40	/* No MIDI control */
#define VS_VIEW_EXPAND	(VS_VIEW_HFILL|VS_VIEW_VFILL)

	int wPre, hPre;			/* Requested geometry */
	Uint xVis;			/* # of on-screen frames */
	AG_Rect rFrames;		/* Video frames area */
	AG_Rect rAudio;			/* Audio waveform area */
	AG_Scrollbar *sb;		/* Scrollbar */
	int incr;			/* Scrolling increment */
	VS_Clip *clip;			/* Video clip to display */
	AG_PopupMenu *menu;		/* Popup menu */
	int xSel;			/* Last selected frame */
	int kbdCenter, kbdOffset, kbdDir;
	AG_Timeout toKbdMove;
} VS_View;

__BEGIN_DECLS
extern AG_WidgetClass vsViewClass;

VS_View *VS_ViewNew(void *, Uint, VS_Clip *);
void     VS_ViewSizeHint(VS_View *, Uint, Uint);
void     VS_ViewSetIncrement(VS_View *, int);
void     VS_Status(void *, const char *, ...);
__END_DECLS

#endif /* _VISLAK_VIEW_H_ */
