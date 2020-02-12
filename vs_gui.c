/*
 * Copyright (c) 2013 Hypertriton, Inc. <http://hypertriton.com/>
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
 * Graphical user interface.
 */

#include <agar/core.h>
#include <agar/gui.h>

#include "vislak.h"
#include "icons.h"
#include "icons_data.h"

#include <string.h>

AG_Object vsVfsRoot;			/* General-purpose VFS */

static int nEditorWindows = 0;

/*
 * Display "Save changes?" dialog on exit.
 */
static void
CloseObject(AG_Event *event)
{
	AG_Window *win = AG_PTR(1);
	AG_Object *obj = AG_PTR(2);
	int save = AG_INT(3);

	if (save) {
		if (AG_ObjectSave(obj) == -1) {
			AG_TextMsgFromError();	/* TODO suggest "save as" */
			return;
		}
	}
	AG_ObjectDetach(win);
	AG_ObjectDelete(obj);

	if (--nEditorWindows == 0)
		AG_Terminate(0);
}
static void
WindowClose(AG_Event *event)
{
	AG_Window *win = AG_SELF();
	AG_Object *obj = AG_PTR(1);
	AG_Event ev;
	AG_Button *bOpts[3];
	AG_Window *wDlg;

	if (!AG_ObjectChanged(obj)) {
		AG_EventArgs(&ev, "%p,%p,%i", win, obj, 0);
		CloseObject(&ev);
		return;
	}
	wDlg = AG_TextPromptOptions(bOpts, 3, _("Save changes to %s?"), OBJECT(obj)->name);
	AG_WindowAttach(win, wDlg);
	AG_ButtonText(bOpts[0], _("Save"));
	AG_SetEvent(bOpts[0], "button-pushed", CloseObject, "%p,%p,%i", win, obj, 1);
	AG_WidgetFocus(bOpts[0]);
	AG_ButtonText(bOpts[1], _("Discard"));
	AG_SetEvent(bOpts[1], "button-pushed", CloseObject, "%p,%p,%i", win, obj, 0);
	AG_ButtonText(bOpts[2], _("Cancel"));
	AG_SetEvent(bOpts[2], "button-pushed", AGWINDETACH(wDlg));
}

/* Open a Vislak object for edition. */
AG_Window *
VS_GUI_OpenObject(void *p)
{
	AG_Object *obj = p;
	AG_Window *win = NULL;
	AG_Widget *wEdit;

	/* Invoke edit(), which may return a Window or some other Widget. */
	Verbose("Opening %s (%s)\n", obj->name, obj->cls->name);
	if ((wEdit = obj->cls->edit(obj)) == NULL) {
		AG_SetError("%s no edit()", obj->cls->name);
		return (NULL);
	}
	if (AG_OfClass(wEdit, "AG_Widget:AG_Window:*")) {
		win = (AG_Window *)wEdit;
	} else if (AG_OfClass(wEdit, "AG_Widget:*")) {
		win = AG_WindowNew(AG_WINDOW_MAIN);
		AG_ObjectAttach(win, wEdit);
	} else {
		AG_SetError("%s: edit() illegal object", obj->cls->name);
		return (NULL);
	}
	AG_WindowSetCaptionS(win,
	    AG_Defined(obj,"archive-path") ?
	    AG_ShortFilename(AG_GetStringP(obj,"archive-path")) : obj->name);

	AG_SetEvent(win, "window-close", WindowClose, "%p", obj);
	AG_SetPointer(win, "object", obj);
	AG_PostEvent(obj, "edit-open", NULL);

	AG_WindowShow(win);
	return (win);
}

/* Create a new instance of a native Vislak object class. */
void
VS_GUI_NewObject(AG_Event *event)
{
	AG_ObjectClass *cl = AG_PTR(1);
	AG_Object *obj;

	obj = AG_ObjectNew(&vsVfsRoot, NULL, cl);
	if (obj == NULL ||
	    VS_GUI_OpenObject(obj) == NULL) {
		goto fail;
	}
	return;
fail:
	AG_TextError(_("Failed to create object: %s"), AG_GetError());
	if (obj != NULL) { AG_ObjectDelete(obj); }
}

/* Load a native Vislak object file. */
void
VS_GUI_LoadObject(AG_Event *event)
{
	AG_ObjectClass *cl = AG_PTR(1);
	char *path = AG_STRING(2);
	AG_Object *obj;

	Verbose("Loading %s object from %s\n", cl->name, path);

	if ((obj = AG_ObjectNew(&vsVfsRoot, NULL, cl)) == NULL) {
		goto fail;
	}
	if (AG_ObjectLoadFromFile(obj, path) == -1) {
		AG_SetError("%s: %s", AG_ShortFilename(path), AG_GetError());
		goto fail;
	}
	AG_ObjectSetArchivePath(obj, path);
	AG_ObjectSetNameS(obj, AG_ShortFilename(path));

	if (VS_GUI_OpenObject(obj) == NULL) {
		goto fail;
	}
	return;
fail:
	AG_TextError(_("Could not open object: %s"), AG_GetError());
/*	if (obj != NULL) { AG_ObjectDelete(obj); } */
}

/* "Open..." dialog */
void
VS_GUI_OpenDlg(AG_Event *event)
{
	AG_Window *win;
	AG_FileDlg *fd;
	int j;

	win = AG_WindowNew(0);
	AG_WindowSetCaptionS(win, _("Open..."));

	fd = AG_FileDlgNewMRU(win, "vislak.mru.files",
	    AG_FILEDLG_LOAD|AG_FILEDLG_CLOSEWIN|AG_FILEDLG_EXPAND);
	AG_FileDlgSetOptionContainer(fd, AG_BoxNewVert(win, AG_BOX_HFILL));
	
	for (j = 0; j < agFileExtCount; j++) {
		const AG_FileExtMapping *me = &agFileExtMap[j];
		char lbl[64], ext[16];

		snprintf(lbl, sizeof(lbl), _("Vislak %s file"), me->descr);
		Strlcpy(ext, "*", sizeof(ext));
		Strlcat(ext, me->ext, sizeof(ext));
		AG_FileDlgAddType(fd, lbl, ext,
		    VS_GUI_LoadObject, "%p", me->cls);
	}
	AG_WindowShow(win);
}

/* Save an object file in native Vislak format. */
static void
SaveNativeObject(AG_Event *event)
{
	AG_Object *obj = AG_PTR(1);
	char *path = AG_STRING(2);
	AG_Window *wEdit;

	if (AG_ObjectSaveToFile(obj, path) == -1) {
		AG_TextError("%s: %s", AG_ShortFilename(path), AG_GetError());
	}
	AG_ObjectSetArchivePath(obj, path);
	AG_ObjectSetNameS(obj, AG_ShortFilename(path));

	if ((wEdit = AG_WindowFindFocused()) != NULL)
		AG_WindowSetCaptionS(wEdit, AG_ShortFilename(path));
}

/* "Save as..." dialog. */
void
VS_GUI_SaveAsDlg(AG_Event *event)
{
	char defDir[AG_PATHNAME_MAX];
	AG_Object *obj = AG_PTR(1);
	AG_Window *win;
	AG_FileDlg *fd;
	int j;

	if (obj == NULL) {
		AG_TextError(_("No document is selected for saving."));
		return;
	}

	AG_GetString(agConfig, "save-path", defDir, sizeof(defDir));

	win = AG_WindowNew(0);
	AG_WindowSetCaption(win, _("Save %s as..."), obj->name);

	fd = AG_FileDlgNew(win, AG_FILEDLG_SAVE|AG_FILEDLG_CLOSEWIN|
	                        AG_FILEDLG_EXPAND);
	AG_FileDlgSetOptionContainer(fd, AG_BoxNewVert(win, AG_BOX_HFILL));
	AG_FileDlgSetDirectoryMRU(fd, "vislak.mru.files", defDir);
	
	for (j = 0; j < agFileExtCount; j++) {
		const AG_FileExtMapping *me = &agFileExtMap[j];
		AG_ObjectClass *cls = (AG_ObjectClass *)me->cls;
		char lbl[64], ext[16];

		if (AG_OfClass(obj, cls->hier)) {
			snprintf(lbl, sizeof(lbl), _("Vislak %s file"),
			    me->descr);
			Strlcpy(ext, "*", sizeof(ext));
			Strlcat(ext, me->ext, sizeof(ext));

			AG_FileDlgAddType(fd, lbl, ext, 
			    SaveNativeObject, "%p", obj);
		}
	}
	AG_WindowShow(win);
}

/* "Save" action */
void
VS_GUI_Save(AG_Event *event)
{
	AG_Object *obj = AG_PTR(1);
	
	if (obj == NULL) {
		AG_TextError(_("No document is selected for saving."));
		return;
	}
	if (!AG_Defined(obj, "archive-path")) {
		VS_GUI_SaveAsDlg(event);
		return;
	}
	if (AG_ObjectSave(obj) == -1) {
		AG_TextError(_("Error saving object: %s"), AG_GetError());
	} else {
		AG_TextTmsg(AG_MSG_INFO, 1250, _("Saved %s successfully"),
		    AG_GetStringP(obj, "archive-path"));
	}
}

static void
SelectedFont(AG_Event *event)
{
	AG_Window *win = AG_PTR(1);

	AG_SetString(agConfig, "font.face",  OBJECT(agDefaultFont)->name);
	AG_SetInt(agConfig, "font.size", agDefaultFont->spec.size);
	AG_SetUint(agConfig, "font.flags", agDefaultFont->flags);
	(void)AG_ConfigSave();

	AG_TextWarning("default-font-changed",
	    _("The default font has been changed.\n"
	      "Please restart application for this change to take effect."));
	AG_ObjectDetach(win);
}

/* "Select font" dialog */
void
VS_GUI_SelectFontDlg(AG_Event *event)
{
	AG_Window *win;
	AG_FontSelector *fs;
	AG_Box *hBox;

	win = AG_WindowNew(0);
	AG_WindowSetCaptionS(win, _("Font selection"));

	fs = AG_FontSelectorNew(win, AG_FONTSELECTOR_EXPAND);
	AG_BindPointer(fs, "font", (void *)&agDefaultFont);

	hBox = AG_BoxNewHoriz(win, AG_BOX_HFILL|AG_BOX_HOMOGENOUS);
	AG_ButtonNewFn(hBox, 0, _("OK"), SelectedFont, "%p", win);
	AG_ButtonNewFn(hBox, 0, _("Cancel"), AG_WindowCloseGenEv, "%p", win);
	AG_WindowShow(win);
}

/* Build a generic "File" menu. */
void
VS_FileMenu(AG_MenuItem *m, void *obj)
{
	const AG_FileExtMapping *me = NULL;
	int j;

	for (j = 0; j < agFileExtCount; j++) {
		me = &agFileExtMap[j];
		if (!me->editDirect) {
			continue;
		}
		AG_MenuAction(m,
		    AG_Printf("New %s...", me->descr),
		    agIconDoc.s,
		    VS_GUI_NewObject, "%p", me->cls);
	}
	
	AG_MenuSeparator(m);

	AG_MenuActionKb(m, _("Open..."), agIconLoad.s,
	    AG_KEY_O, AG_KEYMOD_CTRL,
	    VS_GUI_OpenDlg, NULL);
	AG_MenuActionKb(m, _("Save"), agIconSave.s,
	    AG_KEY_S, AG_KEYMOD_CTRL,
	    VS_GUI_Save, "%p", obj);
	AG_MenuAction(m, _("Save as..."), agIconSave.s,
	    VS_GUI_SaveAsDlg, "%p", obj);
}

/* Build a generic "Edit" menu. */
void
VS_EditMenu(AG_MenuItem *m, void *obj)
{
#if 0
	AG_MenuActionKb(m, _("Undo"), agIconUp.s, AG_KEY_Z, AG_KEYMOD_CTRL,
	    VS_GUI_Undo, "%p", obj);
	AG_MenuActionKb(m, _("Redo"), agIconDown.s, AG_KEY_R, AG_KEYMOD_CTRL,
	    VS_GUI_Redo, "%p", obj);
#endif
	AG_MenuSeparator(m);
	AG_MenuAction(m, _("Select font..."), agIconMagnifier.s,
	    VS_GUI_SelectFontDlg, NULL);
}

/* Initialize Vislak GUI globals. */
void
VS_InitGUI(void)
{
	if (agGUI) {
		AG_RegisterClass(&vsViewClass);
		AG_RegisterClass(&vsPlayerClass);
		vsIcon_Init();
	}
	AG_ObjectInitStatic(&vsVfsRoot, NULL);
	AG_ObjectSetName(&vsVfsRoot, "Vislak VFS");
}

/* Release Vislak GUI globals. */
void
VS_DestroyGUI(void)
{
	if (agGUI) {
//		AG_UnregisterClass(&vsPlayerClass);
//		AG_UnregisterClass(&vsViewClass);
	}
//	AG_ObjectDestroy(&vsVfsRoot);
}
