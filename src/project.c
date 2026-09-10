#include "project.h"

VNG_NODE *vng_projects = NULL;

/* What the sidebar will show as a file. Directories are always shown, whatever they
 * hold - deciding otherwise would mean walking a folder to find out whether it is worth
 * listing, which is the recursive scan the lazy expansion exists to avoid. */
static const char *const IMAGE_EXT[] = {
	"png", "jpg", "jpeg", "webp", "avif", "tif", "tiff", "gif", "bmp", "tga",
	"qoi", "ico", "cur", "pcx", "svg", "xcf", "lbm", "pnm", "ppm", "pgm", "pbm",
	"xpm", "jxl"
};

bool project_is_image (const char *name)
{
	const char *dot = NULL;
	for (const char *p = name; *p; p++)
		if (*p == '.') dot = p;
	if (!dot || !dot[1]) return false;

	for (size_t i = 0; i < SDL_arraysize(IMAGE_EXT); i++)
		if (SDL_strcasecmp(dot + 1, IMAGE_EXT[i]) == 0)
			return true;
	return false;
}

static void node_name (VNG_NODE *n, const char *path)
{
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;

	/* A root can be a drive or end in a separator, leaving nothing after the last one.
	 * Showing an empty row would be worse than showing the whole path. */
	SDL_strlcpy(n->name, *base ? base : path, sizeof n->name);
}

static VNG_NODE *node_new (const char *path, bool is_dir)
{
	VNG_NODE *n = (VNG_NODE *) SDL_calloc(1, sizeof *n);
	if (!n) return NULL;

	n->path = SDL_strdup(path);
	if (!n->path) { SDL_free(n); return NULL; }

	n->is_dir = is_dir;
	node_name(n, path);
	return n;
}

static void node_free (VNG_NODE *n)
{
	while (n) {
		VNG_NODE *next = n->next;
		node_free(n->child);
		SDL_free(n->path);
		SDL_free(n);
		n = next;
	}
}

/* Directories before files, then by name, both case insensitively.
 *
 * SDL_EnumerateDirectory promises no order at all - it hands back whatever the platform
 * gives it, which differs between Windows and ext4 and can differ between two runs. A
 * list that reshuffles itself is unusable for pointing at things, so the order is
 * imposed here rather than hoped for. */
static int SDLCALL node_cmp (const void *a, const void *b)
{
	const VNG_NODE *x = *(VNG_NODE * const *)a;
	const VNG_NODE *y = *(VNG_NODE * const *)b;

	if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
	return SDL_strcasecmp(x->name, y->name);
}

static SDL_EnumerationResult SDLCALL scan_cb (void *ud, const char *dirname, const char *fname)
{
	VNG_NODE *parent = (VNG_NODE *) ud;
	char      full[1024];

	/* dirname arrives with a trailing separator already, so this does not add one. */
	SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);

	SDL_PathInfo info;
	if (!SDL_GetPathInfo(full, &info))
		return SDL_ENUM_CONTINUE;     /* unreadable entry: skip it, do not fail the scan */

	bool is_dir = (info.type == SDL_PATHTYPE_DIRECTORY);
	if (!is_dir && !project_is_image(fname))
		return SDL_ENUM_CONTINUE;

	/* Dot files and dot folders stay out. A project folder next to a .git holds tens of
	 * thousands of entries no one wants to page through. */
	if (fname[0] == '.')
		return SDL_ENUM_CONTINUE;

	VNG_NODE *n = node_new(full, is_dir);
	if (!n) return SDL_ENUM_FAILURE;

	n->next = parent->child;      /* pushed on the front; the sort below fixes order */
	parent->child = n;
	return SDL_ENUM_CONTINUE;
}

static void node_sort_children (VNG_NODE *parent)
{
	int count = 0;
	for (VNG_NODE *p = parent->child; p; p = p->next) count++;
	if (count < 2) return;

	VNG_NODE **arr = (VNG_NODE **) SDL_malloc((size_t)count * sizeof *arr);
	if (!arr) return;             /* unsorted is ugly; no list at all would be worse */

	int i = 0;
	for (VNG_NODE *p = parent->child; p; p = p->next) arr[i++] = p;
	SDL_qsort(arr, (size_t)count, sizeof *arr, node_cmp);

	for (i = 0; i < count - 1; i++) arr[i]->next = arr[i + 1];
	arr[count - 1]->next = NULL;
	parent->child = arr[0];

	SDL_free(arr);
}

static void node_scan (VNG_NODE *dir)
{
	if (!dir->is_dir || dir->scanned) return;

	dir->scanned = true;          /* set first: a directory that fails to read must not
	                               * be retried on every single frame */
	SDL_EnumerateDirectory(dir->path, scan_cb, dir);
	node_sort_children(dir);
}

/*
 * HOW DEEP A RE-READ MAY GO.
 *
 * The recursion below follows what a person has EXPANDED, so in practice it is a handful of
 * levels - but the tree comes from a folder somebody dropped in, and a symlink loop or a
 * pathological hierarchy must run out of counter rather than out of C stack. The sidebar's
 * own walk caps itself the same way, for the same reason.
 */
#define RESCAN_DEEP 32

static void node_rescan (VNG_NODE *dir, int deep);

/*
 * Was a directory of that name open here before, and is it worth following down?
 *
 * Matched by NAME and not by pointer, because the old nodes are about to be freed and the new
 * ones are different memory describing the same folder. The name is what the two share.
 */
static bool was_open (VNG_NODE *old, const char *name)
{
	for (VNG_NODE *p = old; p; p = p->next)
		if (p->is_dir && p->open && SDL_strcmp(p->name, name) == 0) return true;
	return false;
}

static void node_rescan (VNG_NODE *dir, int deep)
{
	if (!dir || !dir->is_dir || deep >= RESCAN_DEEP) return;

	/*
	 * THE OLD CHILDREN ARE HELD, NOT FREED, UNTIL THE NEW ONES ARE IN.
	 *
	 * They are the only record of which subfolders were open, and that state is the whole
	 * point of a refresh being different from a collapse: a person who has drilled four
	 * folders down to a spritesheet must not have to do it again because they hid the panel.
	 */
	VNG_NODE *old = dir->child;
	dir->child   = NULL;
	dir->scanned = false;

	node_scan(dir);

	for (VNG_NODE *c = dir->child; c; c = c->next) {
		if (!c->is_dir || !was_open(old, c->name)) continue;

		c->open = true;
		node_rescan(c, deep + 1);
	}

	node_free(old);
}

void project_toggle (VNG_NODE *dir)
{
	if (!dir || !dir->is_dir) return;

	/* OPENING IS ASKING WHAT IS IN IT, so opening goes and looks - see project.h. Closing
	 * reads nothing: it is a walk of the disk to draw a row that says nothing is showing. */
	if (!dir->open) {
		if (dir->scanned) node_rescan(dir, 0);
		else              node_scan(dir);
	}

	dir->open = !dir->open;
}

void project_refresh (void)
{
	for (VNG_NODE *p = vng_projects; p; p = p->next)
		if (p->is_dir && p->open && p->scanned) node_rescan(p, 0);
}

bool project_add (const char *dir)
{
	if (!dir || !*dir) return false;

	SDL_PathInfo info;
	if (!SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY)
		return false;

	for (VNG_NODE *p = vng_projects; p; p = p->next)
		if (SDL_strcmp(p->path, dir) == 0)
			return false;                  /* already here */

	VNG_NODE *n = node_new(dir, true);
	if (!n) return false;

	/* Appended, not pushed: the list is what the person will read top to bottom, and a
	 * new project belonging at the bottom is what every editor does. */
	VNG_NODE **tail = &vng_projects;
	while (*tail) tail = &(*tail)->next;
	*tail = n;

	project_save();
	return true;
}

void project_remove (VNG_NODE *root)
{
	VNG_NODE **pp = &vng_projects;

	while (*pp && *pp != root) pp = &(*pp)->next;
	if (!*pp) return;

	*pp = root->next;
	root->next = NULL;      /* or node_free would take the rest of the list with it */
	node_free(root);

	project_save();
}

void project_move (VNG_NODE *root, int index)
{
	if (!root) return;

	/*
	 * A SINGLY LINKED LIST HAS NO prev, so the handle on a node is the POINTER THAT REACHES
	 * IT - `&vng_projects` for the first, `&previous->next` for the rest. Walking with a
	 * double pointer finds that handle and counts the roots in the same pass, and it is what
	 * lets the unlink below be one write with no special case for the head.
	 *
	 * The tab row keeps a prev and can splice from the node itself; this list never needed
	 * one for anything else, and a back pointer added for one gesture is a second thing to
	 * keep true on every add and every remove.
	 */
	int         n  = 0;
	VNG_NODE  **at = NULL;

	for (VNG_NODE **pp = &vng_projects; *pp; pp = &(*pp)->next, n++)
		if (*pp == root) at = pp;

	if (!at) return;   /* not a root - a subfolder has no place of its own to move to */

	if (index < 0)   index = 0;
	if (index >= n)  index = n - 1;

	*at = root->next;
	root->next = NULL;

	/* Counted over the list WITH root already out of it, which is what makes dragging
	 * downwards land past the root it was dropped on rather than in front of it. */
	VNG_NODE **pp = &vng_projects;
	for (int i = 0; i < index && *pp; i++) pp = &(*pp)->next;

	root->next = *pp;
	*pp = root;
}

/* The file lives beside the executable rather than in a user config directory, because
 * it is named like a document and behaves like one: it is the list of folders THIS copy
 * of Vangopix knows about, and a copy carried on a stick should carry its projects. The
 * cost is that an installation into a read-only directory cannot save it. */
static char *proj_path (void)
{
	return vangopix_asset(VNG_PROJ_FILE);
}

bool project_load (void)
{
	char *file = proj_path();
	if (!file) return false;

	size_t len  = 0;
	char  *text = (char *) SDL_LoadFile(file, &len);
	SDL_free(file);
	if (!text) return false;      /* no file yet is the normal first run, not an error */

	char *line = text;
	while (line < text + len) {
		char *end = line;
		while (end < text + len && *end != '\n' && *end != '\r') end++;
		*end = '\0';

		/* '#' opens a comment so the header this program writes survives a round trip,
		 * and so a person can park a path without deleting it. */
		if (*line && *line != '#') {
			SDL_PathInfo info;
			if (SDL_GetPathInfo(line, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
				VNG_NODE *n = node_new(line, true);
				if (n) {
					VNG_NODE **tail = &vng_projects;
					while (*tail) tail = &(*tail)->next;
					*tail = n;
				}
			} else {
				SDL_Log("project: dropping missing path '%s'", line);
			}
		}

		line = end + 1;
		while (line < text + len && (*line == '\n' || *line == '\r')) line++;
	}

	SDL_free(text);
	return true;
}

bool project_save (void)
{
	char *file = proj_path();
	if (!file) return false;

	SDL_IOStream *io = SDL_IOFromFile(file, "w");
	if (!io) {
		SDL_Log("project: cannot write %s: %s", file, SDL_GetError());
		SDL_free(file);
		return false;
	}
	SDL_free(file);

	const char *head = "# Vangopix projects. One folder per line; '#' is a comment.\n";
	SDL_WriteIO(io, head, SDL_strlen(head));

	for (VNG_NODE *p = vng_projects; p; p = p->next) {
		SDL_WriteIO(io, p->path, SDL_strlen(p->path));
		SDL_WriteIO(io, "\n", 1);
	}

	SDL_CloseIO(io);
	return true;
}

void project_free (void)
{
	node_free(vng_projects);
	vng_projects = NULL;
}
