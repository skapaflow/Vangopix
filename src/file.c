#include "file.h"

/*
 * THE CALLBACK DOES NOT RUN ON THE MAIN THREAD, AND EVERYTHING HERE FOLLOWS FROM THAT.
 *
 * SDL says it plainly: "the callback may be invoked from the same thread or from a
 * different one, depending on the OS's constraints" (SDL_dialog.h). So the callback may
 * not touch the tab list, may not build a surface, may not write a file and may not
 * update the title - none of that is safe from a thread the rest of this program does
 * not know exists.
 *
 * What it does instead is the one thing SDL guarantees from any thread: it pushes an
 * event ("It is safe to call this function from any thread", SDL_events.h). The path
 * rides along as a copy, the main loop picks it up in file_event on the next turn of
 * vangopix_input, and every line that matters runs where everything else runs.
 *
 * The request travels as an ID, NOT AS A TAB POINTER. Between the asking and the
 * answering the person may have closed that tab, and its memory may already belong to a
 * new one at the same address - a pointer checked against the list would pass that test
 * and write the wrong document to the chosen file. See vng_tab_by_id.
 */

/* Two private types out of one contiguous range - SDL_RegisterEvents "returns the
 * beginning event number". They are told apart by type rather than by a tag inside one
 * type because their payloads differ: a save answer carries a request, an open answer
 * carries only a path. */
static Uint32 ev_saved = 0;   /* where to write a document */
static Uint32 ev_open  = 0;   /* a file to be turned into a tab */

typedef struct {
	Uint32 tab_id;
	bool   close_after;   /* the save was asked for by the close confirmation */
} SAVE_REQ;

/* Must outlive the call: SDL_ShowSaveFileDialog says the filter list has to stay valid
 * until the callback runs, which is on another thread, some time later. */
static const SDL_DialogFileFilter save_filters[] = {
	{ "PNG image",  "png"      },
	{ "JPEG image", "jpg;jpeg" },
	{ "WebP image", "webp"     },
	{ "AVIF image", "avif"     },
	{ "Bitmap",     "bmp"      },
	{ "Targa",      "tga"      },
	{ "All files",  "*"        },
};

/* One broad entry and an escape hatch. Listing every suffix SDL3_image reads would be a
 * menu of twenty lines for a question that is nearly always "the images, please". */
static const SDL_DialogFileFilter open_filters[] = {
	{ "Images",    "png;jpg;jpeg;gif;bmp;tga;webp;avif;tif;tiff;qoi;pcx;xpm;ico;cur" },
	{ "All files", "*" },
};

bool file_init (void)
{
	Uint32 base = SDL_RegisterEvents(2);
	if (!base) {
		SDL_Log("SDL_RegisterEvents: %s", SDL_GetError());
		return false;
	}

	ev_saved = base;
	ev_open  = base + 1;
	return true;
}

static void say (SDL_MessageBoxFlags kind, const char *msg)
{
	SDL_ShowSimpleMessageBox(kind, VNG_NAME, msg, vng_win);
}

/*
 * The write itself.
 *
 * SDL_CreateSurfaceFrom does not copy: the surface points straight at the document's own
 * buffer, which is what ARGB8888 was chosen for in the first place. It follows that the
 * surface must not outlive that buffer and that no resize may happen underneath it -
 * both hold here because this only ever runs on the main thread, between two events.
 */
static bool write_file (VNG_TAB *t, const char *path)
{
	SDL_Surface *s = SDL_CreateSurfaceFrom(t->w, t->h, SDL_PIXELFORMAT_ARGB8888,
	                                       t->pixels, t->w * (int)sizeof(Uint32));
	if (!s) {
		say(SDL_MESSAGEBOX_ERROR, SDL_GetError());
		return false;
	}

	bool ok = IMG_Save(s, path);

	/* Destroys the surface, never the pixels - those belong to the document. */
	SDL_DestroySurface(s);

	/* A save that fails silently is the same as no save at all, discovered later. */
	if (!ok) say(SDL_MESSAGEBOX_ERROR, SDL_GetError());
	return ok;
}

static bool save_to (VNG_TAB *t, const char *path)
{
	if (!write_file(t, path)) return false;

	vng_tab_set_path(t, path);
	t->dirty = false;
	vng_tab_title();
	return true;
}

/* Runs on WHATEVER THREAD THE OS CHOSE. Nothing here may touch this program's state. */
static void SDLCALL on_chosen (void *userdata, const char * const *filelist, int filter)
{
	(void)filter;

	SAVE_REQ *req = (SAVE_REQ *)userdata;

	/* NULL is an error, a pointer to NULL is a cancel. Neither is worth a message box:
	 * one the person just did on purpose, and the other SDL has already logged. */
	if (!filelist || !filelist[0] || !ev_saved) {
		SDL_free(req);
		return;
	}

	SDL_Event e;
	SDL_zero(e);
	e.type       = ev_saved;
	e.user.data1 = SDL_strdup(filelist[0]);   /* the list is freed when this returns */
	e.user.data2 = req;

	if (!e.user.data1 || !SDL_PushEvent(&e)) {
		SDL_free(e.user.data1);
		SDL_free(req);
	}
}

void file_save_as (VNG_TAB *t)
{
	if (!t) return;

	SAVE_REQ *req = (SAVE_REQ *) SDL_calloc(1, sizeof *req);
	if (!req) return;
	req->tab_id = t->id;

	/* Starts where the document already lives. NULL leaves the choice to the platform,
	 * which is the right answer for a document that has never been anywhere. */
	SDL_ShowSaveFileDialog(on_chosen, req, vng_win, save_filters,
	                       (int)SDL_arraysize(save_filters), t->path);
}

void file_save (VNG_TAB *t)
{
	if (!t) return;

	/* A tab with no path has never been asked where it lives, so CTRL+S has to ask. */
	if (!t->path) { file_save_as(t); return; }
	save_to(t, t->path);
}

/* Also on WHATEVER THREAD THE OS CHOSE - vng_tab_open builds a texture, so it cannot
 * possibly be called from here. One event per file, in the order they were chosen: the
 * queue is FIFO, so the tabs come out in that same order. */
static void SDLCALL on_open_chosen (void *userdata, const char * const *filelist,
                                    int filter)
{
	(void)userdata; (void)filter;

	if (!filelist || !ev_open) return;

	for (int i = 0; filelist[i]; i++) {
		SDL_Event e;
		SDL_zero(e);
		e.type       = ev_open;
		e.user.data1 = SDL_strdup(filelist[i]);   /* the list dies with this call */

		if (!e.user.data1) return;
		if (!SDL_PushEvent(&e)) {
			SDL_free(e.user.data1);
			return;   /* the queue is full or filtered; the rest would fail the same */
		}
	}
}

void file_open_ask (void)
{
	/* allow_many, because that is already how this program behaves: every path on the
	 * command line becomes its own tab, and a person picking six sprites means six. */
	SDL_ShowOpenFileDialog(on_open_chosen, NULL, vng_win, open_filters,
	                       (int)SDL_arraysize(open_filters), NULL, true);
}

bool file_event (const SDL_Event *e)
{
	if (ev_open && e->type == ev_open) {
		char *path = (char *) e->user.data1;
		if (path) vng_tab_open(path);   /* on the main thread, where a texture is legal */
		SDL_free(path);
		return true;
	}

	if (!ev_saved || e->type != ev_saved) return false;

	char     *path = (char *)     e->user.data1;
	SAVE_REQ *req  = (SAVE_REQ *) e->user.data2;

	/* Back on the main thread, and only now is it safe to ask whether that document is
	 * still open. It may not be: the dialog was up, and a dialog is not a lock. */
	VNG_TAB *t = req ? vng_tab_by_id(req->tab_id) : NULL;

	if (t && path && save_to(t, path) && req->close_after)
		vng_tab_close(t);   /* the question was already asked; the work is on disk now */

	SDL_free(path);
	SDL_free(req);
	return true;
}

/*
 * The three-button question every editor asks. It is the system's box, so it is drawn
 * outside the window and needs no keyboard owner of its own: it takes the keyboard from
 * the whole program while it is up, and hands it back.
 */
enum { ANS_CANCEL = 0, ANS_SAVE, ANS_DISCARD };

static int ask_unsaved (const char *msg)
{
	const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, ANS_CANCEL,  "Cancel"  },
		{ 0,                                       ANS_DISCARD, "Discard" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, ANS_SAVE,    "Save"    },
	};
	const SDL_MessageBoxData data = {
		SDL_MESSAGEBOX_WARNING, vng_win, VNG_NAME, msg,
		(int)SDL_arraysize(buttons), buttons, NULL
	};

	int answer = ANS_CANCEL;

	/* A box that could not be shown must not be read as consent. Cancelling is the only
	 * answer that cannot lose anything. */
	if (!SDL_ShowMessageBox(&data, &answer)) {
		SDL_Log("SDL_ShowMessageBox: %s", SDL_GetError());
		return ANS_CANCEL;
	}
	return answer;
}

void file_close_tab (VNG_TAB *t)
{
	if (!t) return;

	if (!t->dirty) { vng_tab_close(t); return; }

	char msg[256];
	SDL_snprintf(msg, sizeof msg, "%s has unsaved changes.", t->name);

	switch (ask_unsaved(msg)) {

	case ANS_DISCARD:
		vng_tab_close(t);
		break;

	case ANS_SAVE:
		/* A tab with a path is written here and now. One without has to be asked about
		 * first, and that answer arrives on another turn of the loop - so the close
		 * travels with the request instead of happening immediately, or Save would
		 * quietly behave as Discard on exactly the documents never saved before. */
		if (t->path) {
			if (save_to(t, t->path)) vng_tab_close(t);
		} else {
			SAVE_REQ *req = (SAVE_REQ *) SDL_calloc(1, sizeof *req);
			if (!req) break;
			req->tab_id      = t->id;
			req->close_after = true;

			SDL_ShowSaveFileDialog(on_chosen, req, vng_win, save_filters,
			                       (int)SDL_arraysize(save_filters), NULL);
		}
		break;

	default:
		break;   /* cancelled: the tab stays exactly as it was */
	}
}

bool file_confirm_quit (void)
{
	int dirty = 0;
	for (VNG_TAB *p = vng_tabs; p; p = p->next)
		if (p->dirty) dirty++;

	if (dirty == 0) return true;

	char msg[128];
	SDL_snprintf(msg, sizeof msg, "%d document%s with unsaved changes.",
	             dirty, dirty == 1 ? "" : "s");

	/*
	 * Two buttons and not three, and the missing one is deliberate: SAVE ALL would have
	 * to chain one asynchronous dialog per unnamed document behind a quit that has
	 * already been asked for, and then decide what a cancel on the third one means.
	 * Cancel puts the person back in the program, where saving each document is two keys
	 * and they can see which is which.
	 */
	const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT |
		  SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel"           },
		{ 0,                                       1, "Discard and quit" },
	};
	const SDL_MessageBoxData data = {
		SDL_MESSAGEBOX_WARNING, vng_win, VNG_NAME, msg,
		(int)SDL_arraysize(buttons), buttons, NULL
	};

	int answer = 0;
	if (!SDL_ShowMessageBox(&data, &answer)) {
		SDL_Log("SDL_ShowMessageBox: %s", SDL_GetError());
		return false;
	}
	return answer == 1;
}
