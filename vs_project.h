/*	Public domain	*/

typedef enum vs_proc_op {
	VS_PROC_INIT,			/* Initialization */
	VS_PROC_IDLE,			/* Idle */
	VS_PROC_LOAD_VIDEO,		/* Importing video data */
	VS_PROC_LOAD_AUDIO,		/* Importing audio data */
	VS_PROC_TERMINATE		/* Exiting */
} VS_ProcOp;

typedef struct vs_project {
	struct ag_object _inherit;
	Uint flags;
#define VS_PROJECT_RECORDING	0x01	/* Recording mode */
#define VS_PROJECT_LEARNING	0x02	/* Learning mode */
#define VS_PROJECT_PLAYING	0x04	/* Playback in progress */
#define VS_PROJECT_SAVED	0
	int thumbSz;			 /* Video thumbnail size (px) */
	int waveSz;			 /* Audio waveform size (px) */
	int frameRate;			 /* Nominal frame rate */
	double bendSpeed;		 /* Speed bend setting */
	double bendSpeedMax;		 /* Speed bend max */
	VS_Clip *input;		 	 /* Input video streams */
	VS_Clip *output;		 /* Rendered output stream */
	VS_ProcOp procOp;		 /* Status of processing thread */
	AG_Thread procTh;		 /* Processing thread */
	struct {
		struct {
			int val;	 /* Progress value */
			int min, max;	 /* Progress range */
		} progress;
		VS_Player *playerIn;	 /* Playback widget for input */
		VS_Player *playerOut;	 /* Playback widget for output */
		AG_Label *status;
	} gui;
} VS_Project;

__BEGIN_DECLS
extern AG_ObjectClass vsProjectClass;

VS_Project *VS_ProjectNew(void *, const char *);
void        VS_Status(void *, const char *, ...);
void        VS_ProjectRunOperation(VS_Project *, VS_ProcOp);
__END_DECLS
