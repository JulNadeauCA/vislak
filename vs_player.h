/*	Public domain	*/

#ifndef _VISLAK_PLAYER_H_
#define _VISLAK_PLAYER_H_

#include <agar/gui/widget.h>

#include "vs_clip.h"

struct vs_view;

enum vs_player_button {
	VS_PLAYER_REW,		/* Rewind to start of clip */
	VS_PLAYER_PLAY,		/* Start playback */
	VS_PLAYER_STOP,		/* Stop playback */
	VS_PLAYER_FWD,		/* Forward to end of clip */
	VS_PLAYER_REC,		/* Record mode */
	VS_PLAYER_LASTBTN
};

#define VS_PLAYER_SINE_SIZE 200

typedef struct vs_player {
	struct ag_widget _inherit;

	Uint flags;
#define VS_PLAYER_HFILL		0x01
#define VS_PLAYER_VFILL		0x02
#define VS_PLAYER_EXPAND	(VS_PLAYER_HFILL|VS_PLAYER_VFILL)
#define VS_PLAYER_REFRESH	0x04	/* Force refresh */
#define VS_PLAYER_LOD		0x08	/* Hi-quality display */
#define VS_PLAYER_STOPPING	0x10	/* Stopping in progress */

	int wPre, hPre;		/* Requested geometry */
	struct vs_view *vv;	/* Associated view and clip */
	AG_Rect rVid;		/* Video area */
	int xOffsLast;		/* Last drawn frame */
	int suScaled;		/* Scaled surface handle */
	int lodTimeout;		/* Timeout before LOD increase */
	AG_Button *btn[VS_PLAYER_LASTBTN]; /* Control buttons */
	int sine[VS_PLAYER_SINE_SIZE]; 	/* For testing audio */
	int sinePhase;			/* For testing audio */
} VS_Player;

__BEGIN_DECLS
extern AG_WidgetClass vsPlayerClass;
extern int vsPlayerCompensation;

VS_Player *VS_PlayerNew(void *, Uint, struct vs_view *);
void       VS_PlayerSizeHint(VS_Player *, Uint, Uint);
void       VS_Play(VS_Player *);
void       VS_Stop(VS_Player *);
int        VS_PlayAudio(VS_Player *);
int        VS_StopAudio(VS_Player *);
__END_DECLS

#endif /* _VISLAK_PLAYER_H_ */
