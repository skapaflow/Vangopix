#ifndef VANGOPIX_FILE_H
#define VANGOPIX_FILE_H

#include "vangopix.h"
#include "tabs.h"

/*
 * Save, and the two system dialogs that go with it.
 *
 * NOT NAMED io.c, although the Makefile's own list of files to come said so: the
 * compile line carries -Isrc, so a src/io.h would shadow the CRT's own <io.h> for any
 * header that reaches for it, and the failure would arrive as an unrelated error inside
 * a system header.
 *
 * THE DIALOGS ARE THE SYSTEM'S, NOT OURS. SDL_ShowSaveFileDialog and
 * SDL_ShowMessageBox are drawn by the OS, outside the window, so they cost this program
 * no chrome at all and arrive already knowing how to browse a disk, confirm an
 * overwrite and speak the person's language. A file browser drawn in here would be
 * weeks of work to be worse.
 *
 * WHAT THE FILE FORMAT IS: whatever the extension says. IMG_Save picks by suffix and
 * writes png, jpg, webp, avif, bmp, tga, gif, ico and cur. A native Vangopix format
 * earns its place the day there are layers or a timeline to put in it; with one layer,
 * one frame and ARGB, png loses nothing.
 *
 * THE ASYNCHRONOUS PART IS THE WHOLE DESIGN OF THIS FILE - see file.c.
 */

/* Registers the two private event types the dialogs answer through. Called once by
   vangopix_init; without it neither dialog can deliver its answer. */
extern bool file_init (void);

/* CTRL+O. Asks for one or more files at once; each becomes a tab, in the order chosen.
   Returns immediately - like save-as, the answer arrives later, through file_event.

   It is the fourth way into this program, and the only one that does not need a file
   manager already open: the other three are dropping a file, naming one on the command
   line, and clicking one in the project sidebar. */
extern void file_open_ask (void);

/* CTRL+S. Writes to the path the tab already has; a tab that has none is a tab that has
   never been asked where it lives, so this becomes save-as. */
extern void file_save (VNG_TAB *t);

/* CTRL+SHIFT+S. Always asks, even when the tab has a path. Returns immediately: the
   answer arrives later, through file_event. */
extern void file_save_as (VNG_TAB *t);

/*
 * WHAT FILE A SAVE DIALOG'S ANSWER ACTUALLY MEANS.
 *
 * A person who types a bare name with `*.png` showing in the dropdown has said png as
 * plainly as anyone ever says it - and IMG_Save, which picks the format by suffix, would
 * answer "Couldn't determine file type". The convention is the platform's own: the chosen
 * filter supplies the extension when the name does not. Windows and macOS do it inside
 * their dialogs when an app hands them a default extension, and SDL3 exposes no way to ask
 * for that, so it is done here.
 *
 * `filter` is the index SDL reports in the dialog callback, or -1 when the platform does
 * not say which one was picked.
 *
 * Exposed rather than kept private because it is a rule, not a detail - and a rule with
 * three branches is worth pinning down in test/checks.c.
 */
extern void file_with_extension (char *dst, size_t cap, const char *path, int filter);

/* Offered every event, and claims only the private type above. True when it was the
   dialog's answer and it has been dealt with. */
extern bool file_event (const SDL_Event *e);

/* Closes the tab, asking first if that would throw work away. The one way tabs are
   closed from the interface - CTRL+W and the tab's own [x] both come here, so the
   question is asked once in one place. */
extern void file_close_tab (VNG_TAB *t);

/* True when it is all right to stop: nothing is dirty, or the person said so. */
extern bool file_confirm_quit (void);

#endif
