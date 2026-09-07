# Vangopix

A pixel art editor. The name is Van Gogh plus pixel.

Vangopix takes Paint as its starting point and walks toward Aseprite, with one idea held
above the others: **the minimalism is in the interface, not in the features.** There is
no docked panel, no permanent toolbar, no strip of screen reserved for chrome. The sheet
fills the window. Anything that needs a window appears on demand and leaves when it is
done.

That is the complaint this project answers. Aseprite reserves a small rectangle in the
middle for drawing and surrounds it with tools; Vangopix reserves nothing.

## Status

Early, and honest about it: **there is no drawing yet.** What works is everything an
image gets before a brush touches it — opening, viewing, framing, sizing and saving.

| | |
| --- | --- |
| projects | folders in a sidebar, listing only images, remembered in `projects.vngproj` |
| documents | tabs that carry their own camera, reorderable by dragging |
| formats | anything SDL3_image reads: png, webp, avif, tiff, gif, jpg, tga, qoi, ico, pcx, svg, xcf |
| camera | stepped zoom at the cursor, pan, fit and 1:1 |
| canvas | resize by the corner grips, with an optional 8 pixel grid |
| files | open and save through the system's own dialogs; the format follows the extension |
| keyboard | one owner at a time, so a field being open silences every shortcut |
| undo | per document, made of pixel carries and resizes rather than snapshots |
| transparency | checkerboard behind the sheet, black frame around it |
| text | one atlas, packed by stb_truetype |

Next is the pencil itself: the three buffers it draws over and the undo behind them are
already here, so what is left is the tool, a palette, and a colour to draw with.

## Building

C99 and SDL3. One makefile covers Windows, Linux and macOS.

| dependency | Windows | Linux / macOS |
| --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL/releases) 3.4+ | `C:/SDL3`, or `make SDL3=...` | `pkg-config sdl3` |
| [SDL3_image](https://github.com/libsdl-org/SDL_image/releases) 3.4+ | `C:/SDL3_image`, or `make SDL3IMG=...` | `pkg-config sdl3-image` |

```sh
make            # build and run
make release    # build without the console window
make dll        # Windows only: refresh the runtime dlls beside the exe
```

On Windows `SDL3.dll` and `SDL3_image.dll` must sit next to `vangopix.exe`. The
`libpng16` / `libtiff` / `libwebp` / `libavif` dlls from the SDL3_image runtime package
are optional in the literal sense — SDL3_image loads them by name when it meets a file
of that format and runs fine without them. Each one you add adds formats.

SDL3 3.4 reads and writes png in core, so the satellite is not strictly required for
pixel art. It is linked because Vangopix is meant to open anything: webp, avif, tiff,
gif, tga, qoi, ico, pcx, svg, xcf.

## Fonts

The program looks beside the executable for `font/lucon.ttf` first, then
`font/FreeSans.ttf`, and runs without text if it finds neither.

`FreeSans.ttf` is [GNU FreeFont](https://www.gnu.org/software/freefont/) and ships with
the repository so a fresh clone works. `lucon.ttf` is Lucida Console, which comes with
Windows and is **not redistributable** — it is git-ignored, and present only on machines
that already have it.

## Transparency

The sheet sits on a grey checkerboard - the size and the two greys are the ones the
first Vangopix had settled on. Nothing is drawn under the sheet, so a transparent pixel
shows the board through it; white paper underneath would make an empty image and a white
image look identical.

A black frame is drawn just outside the sheet. An image that is mostly alpha has no
visible edge of its own, and those bounds are what a person needs while resizing or
drawing near the border.

## Keys

| | |
| --- | --- |
| `CTRL+N` | new sheet — asks for `w x h` |
| `CTRL+O` | open one or more files |
| `CTRL+S` | save — asks where, the first time |
| `CTRL+SHIFT+S` | save as |
| `CTRL+Z` | undo |
| `CTRL+SHIFT+Z` / `CTRL+Y` | redo |
| `CTRL+W` | close the tab — asks if that would lose work |
| `CTRL+TAB` / `CTRL+SHIFT+TAB` | walk through the tabs |
| `ESC` | show and hide the tab bar |
| `TAB` | show and hide the project sidebar |
| `F1` | toggle the document overlay |
| wheel | zoom in steps, centred on the cursor |
| middle drag, or `SPACE` + left drag | pan |
| `CTRL+0` / `CTRL+1` | fit the sheet / go to 1:1 |
| drag a corner grip | resize the canvas |
| `SHIFT` while dragging a grip | put the corner on the 8 pixel grid |
| drag a file in | open it in a new tab |
| drag a folder in | add it as a project |
| `ENTER` / `ESC` in a field | accept / cancel |

Files named on the command line each open in their own tab, so Vangopix can be
associated with an image extension and handed a whole selection at once.

### The project sidebar

Drop a folder on the window and it becomes a project. `TAB` raises and hides the panel,
which slides in from the left rather than appearing: a panel that covers a third of the
window between two frames leaves nothing on screen to say where it came from. Click a
folder to expand it, click an image to open it in a tab, click a root's `x` to forget it.

While the tab bar is up the sidebar starts below it. Both float over the sheet and both
claim the same corner, and the bar is the one that spans the whole width.

A name too long for the panel is cut and its last column becomes a `~`, the same mark
VagrantUI uses when content overruns its width. The room a name gets accounts for its
own indent, so a deeply nested file is cut shorter than one at the root.

Only images are listed, along with the folders that might contain them. Dot files and
dot folders are skipped, so a `.git` beside the artwork stays out of the way. Folders are
read the first time they are expanded, not when they are added, so dropping a large tree
in is instant.

The list of roots lives in `projects.vngproj` beside the executable: one absolute path
per line, `#` for comments. Paths that no longer exist are dropped on load.

### The tab bar

`ESC` raises it and `ESC` puts it away. Click a tab to select it, drag it sideways to
reorder, click its `x` to close it, click `+` for a new sheet. It floats over the sheet
rather than pushing it down, so it takes no space at all when it is down.

### Opening and saving

`CTRL+O` opens files — several at once, if you pick several. It is the fourth way in and
the only one that does not need a file manager already open: the others are dropping a
file on the window, naming it on the command line, and clicking it in the project sidebar.

`CTRL+S` writes, `CTRL+SHIFT+S` always asks where. There is no menu bar and there is no
file browser drawn in this window: **where to save is asked with the system's own dialog**,
which arrives already knowing how to browse a disk, confirm an overwrite and speak your
language. The format comes from the extension you type — png, jpg, webp, avif, bmp, tga.

Closing a document with unsaved work asks first, and so does quitting. Both are the
system's message box, so they are drawn outside the window and cost no pixels here.

### Undo

`CTRL+Z` and `CTRL+SHIFT+Z` (or `CTRL+Y`). It is not made of snapshots — a snapshot of a
4000x4000 sheet is 64MB and an afternoon is a hundred strokes. A step carries only the
pixels that changed, old colour and new, so a pencil line costs a few kilobytes.

**Resizing the canvas is undone too**, and that was a decision rather than an extra: an
undo that took back a brush stroke and shrugged at a corner grip is one people learn not
to trust. The buffer a resize throws away becomes the undo record itself, so recording one
costs nothing — and it is the only way back, since shrinking a canvas destroys pixels that
nothing can recompute.

The star in the title bar follows the stack: undo back to the point where the file was
last saved and it goes away.

`make test` runs the checks for all of this.

### Who has the keyboard

The mouse is routed by layer — whatever is drawn on top answers a click first, because a
click has a position. A key has none: nothing about `TAB` says whether it belongs to the
sidebar or to a name being typed. So the keyboard has an **owner**, at most one at a time,
and while something owns it the shortcuts are silent. `CTRL+N` opens the first field in
the program: type `128x96`, or one number for a square, `ENTER` to accept and `ESC` to
cancel.

That gate covers more than the shortcuts. `SPACE`+left drag pans and `SHIFT` snaps a grip
to the grid, and both of those read the keyboard *live* rather than waiting for an event —
so both go through the same owner and go quiet while a field is open. A space typed into a
name is a space, not a pan.

The mouse is deliberately left alone: clicking a tab while a field is open still switches
tabs, and the field keeps the keyboard.

### The corner grips

Four small blue squares sit just outside the corners of the sheet, touching it only at
the corner point so they never cover the artwork. Dragging one moves that corner and
leaves the opposite one anchored; the outline and the size readout follow the hand, and
the pixels are only rebuilt on release.

Holding `SHIFT` lands the dragged corner on the nearest multiple of eight — it does not
step by eight, so a canvas of 53 snaps straight to 56 however the drag arrived there.
The readout shows `[8]` while it is on.

## Layout

```
src/
  main.c        the pipeline, and nothing else
  vangopix.c/.h the program: globals, window, renderer, font, argv
  core.c/.h     the frame: input, draw, present
  keys.c/.h     who has the keyboard
  prompt.c/.h   one line of text, asked for and gone
  file.c/.h     save, and the two system dialogs that go with it
  undo.c/.h     the undo stack, per document
  tabs.c/.h     the tabs, which ARE the documents
  tabbar.c/.h   the tab bar: the only file that draws a tab
  project.c/.h  the project folders and projects.vngproj
  sidebar.c/.h  the project panel: the only file that draws a folder
  view.c/.h     the camera: stepped zoom at the cursor, and pan
  resize.c/.h   the corner grips that resize the canvas
  text.c/.h     glyphs packed into one atlas
```

The pairs are deliberate: `tabs`/`tabbar` and `project`/`sidebar` each split a model
from the pixels that draw it. The model knows nothing about the screen, and the panel
knows nothing about what it is listing.

`keys`/`prompt` is the same split one more time: `keys` says who the keyboard belongs to,
`prompt` is the first thing to ask for it. `file` is the only module that writes to disk,
and the only one that talks to a dialog the OS draws.

One file per responsibility, never per size. `CLAUDE.md` carries the decisions behind
these files and the reasoning that produced them.

## Credits

Written by Alberto F. Jr.

Vangopix is the second of its name. The first was written without AI assistance and is
the source of most of the design that survives here. This one is written with
[Claude Code](https://claude.com/claude-code) (Claude Opus 5) as a collaborator, and that
participation is recorded in the commit history rather than hidden.

Text rendering and the camera are adapted from the author's Skyonara engine
(`text/text.c` and `camera/camera.c`), the latter carrying pan and zoom that go back to
the 2016 Skyonara. Text sits on top of
[stb_truetype](https://github.com/nothings/stb) by Sean Barrett (public domain).
