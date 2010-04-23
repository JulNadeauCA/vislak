/*	Public domain	*/

#ifndef _VISLAK_MIDI_H_
#define _VISLAK_MIDI_H_

struct vs_midi_pvt;
struct vs_frame;

#define VS_MIDI_MAXKEYS	256

typedef struct vs_midi {
	Uint flags;
#define VS_MIDI_INPUT	0x01		/* Input in use */
#define VS_MIDI_OUTPUT	0x02		/* Output in use */
	struct vs_midi_pvt *pvt;	/* Driver-specific data */
	struct vs_view *vv;		/* Back pointer to VS_View */
	int keymap[VS_MIDI_MAXKEYS];	/* Key->frame mappings */
} VS_Midi;

__BEGIN_DECLS
VS_Midi *VS_MidiNew(struct vs_view *);
void     VS_MidiDestroy(VS_Midi *);
void     VS_MidiDevicesMenu(VS_Midi *, AG_MenuItem *, Uint);
void     VS_MidiAddKey(VS_Midi *, int, struct vs_frame *);
void     VS_MidiDelKey(VS_Midi *, int);
Uint     VS_MidiClearKeys(VS_Midi *);
__END_DECLS

#endif /* _VISLAK_MIDI_H_ */
