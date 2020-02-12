/*	Public domain	*/

#ifndef _VISLAK_GUI_H_
#define _VISLAK_GUI_H_

__BEGIN_DECLS
extern AG_Object vsVfsRoot;			/* General-purpose VFS */

void       VS_InitGUI(void);
void       VS_DestroyGUI(void);

void       VS_FileMenu(AG_MenuItem *, void *);
void       VS_EditMenu(AG_MenuItem *, void *);

AG_Window *VS_GUI_OpenObject(void *);
void       VS_GUI_NewObject(AG_Event *);
void       VS_GUI_LoadObject(AG_Event *);
void       VS_GUI_OpenDlg(AG_Event *);
void       VS_GUI_SaveAsDlg(AG_Event *);
void       VS_GUI_Save(AG_Event *);
void       VS_GUI_Quit(AG_Event *);
void       VS_GUI_SelectFontDlg(AG_Event *);
__END_DECLS

#endif /* _VISLAK_GUI_H_ */
