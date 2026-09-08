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

Early, but **it draws now.** Eight tools, undo behind them, and everything an image gets
around that — opening, viewing, framing, sizing and saving.

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
| drawing | eight tools on `Q W E R / A S D F`, each with its own tip size |
| selection | mark, move, copy and paste between documents, flip, rotate, invert |
| 1:1 panel | what the art looks like at real size, while you work zoomed in |
| colour | two, one per mouse button, filled by `CTRL`+click and shown as hex |
| transparency | checkerboard behind the sheet, black frame around it |
| text | one atlas, packed by stb_truetype |

Next: a summoned palette for the colours that are not on the sheet yet — drawn by VagrantUI,
gone when it is done — and the other 39 drawing primitives, which are ports onto the same
three buffers the pencil already uses.

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

The program looks beside the executable for `font/DejaVuSansMono.ttf` first, then
`font/FreeSans.ttf`, and runs without text if it finds neither.

**DejaVu Sans Mono** ships with the repository, under the Bitstream Vera / DejaVu licence,
which permits redistribution and asks that the notice travel with the font —
`font/LICENSE_DEJAVU.txt`. It is monospaced, which matters beyond taste: VagrantUI asks for
one fixed character cell and lays its content out in columns from it, so a proportional face
makes every column drift.

`FreeSans.ttf` is [GNU FreeFont](https://www.gnu.org/software/freefont/) and stays as the
one behind it — proportional, so not the right answer, but a corrupt first file should cost
a nicer face and not the ability to read anything on screen.

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
| `Q` `W` `E` `R` | pencil, line, rect, ellipse |
| `A` `S` `D` `F` | eraser, bucket, spray, change-colours |
| `Z` | select |
| `V` | show the 1:1 panel |
| `CTRL+A` `CTRL+C` `CTRL+X` `CTRL+V` | select all, copy, cut, paste |
| `CTRL` + drag a selection | copy it instead of moving it |
| `V` `H` `I` `R`, with a selection | flip, flip, invert, rotate |
| `DELETE`, with a selection | clear it |
| `ESC`, with a selection | let it go |
| `SHIFT` + wheel | tip size, per tool |
| `SHIFT` + click, pencil | draw a straight line from the last point |
| `SHIFT` + drag a line | snap to the pixel-art slopes: 2:1, 1:1, 1:2 |
| `SHIFT` + bucket | barrier fill — spread until it meets your colour |
| `SHIFT`+`TAB` | change-colours: circle or square limiter |
| `SHIFT`+`R` | roll a random colour into slot 1 |
| `M` | average the two slots into slot 1 |
| left drag on the sheet | draw |
| right drag on the sheet | draw colour 2 — transparent to begin with, so it rubs out |
| `CTRL` + left / right click | absorb that colour into 1 or 2; drag to keep absorbing |
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

Type a bare name and the **filter showing in the dropdown supplies the extension**, the way
the platform's own dialogs do it. Type one and it wins over the dropdown: `sprite.webp` with
PNG selected means webp. If neither says anything, it is png.

Closing a document with unsaved work asks first, and so does quitting. Both are the
system's message box, so they are drawn outside the window and cost no pixels here.

### The tools

Eight of them, on a 2x4 block under the left hand while the right hand is on the mouse —
the first Vangopix's own layout, and the reason it is these letters and not the initials of
the names:

| | | | |
| --- | --- | --- | --- |
| `Q` pencil | `W` line | `E` rect | `R` ellipse |
| `A` eraser | `S` bucket | `D` spray | `F` change-colours |
| `Z` select | | | |

Left button draws colour 1, right button draws colour 2. The line, the rectangle and the
ellipse are dragged: press where it starts, drag, release where it ends, and what you see
while dragging is the shape you will get — the whole drag is one undo. The bucket fills the
region it is dropped in. Change-colours keeps working while you hold the button, so sliding
across the sheet takes every colour you pass over — one gesture for an aggressive recolour. The spray keeps building while you hold it, even standing still,
because that is what a spray can does. Change-colours turns every pixel of the colour under
the pointer into your colour — across the whole sheet, or only inside a circle or a square:
`SHIFT`+`TAB` swaps the shape.

**`SHIFT` with the pencil previews a straight line from the last point**, and a click commits
it: draw, move away, hold `SHIFT`, click, and the two ends are joined exactly. What you see
before clicking is pixel for pixel what you get.

**`SHIFT` while dragging a line snaps it to the pixel-art slopes** — horizontal, 2:1, 1:1,
1:2, vertical. The 2:1 is the isometric one: two across for every one down is the line that
comes out clean on a pixel grid, with a run of two identical steps all the way. An arbitrary
angle gives runs of 3, 2, 3, 2, 2 and reads as a wobble.

**`SHIFT` on the bucket makes it a barrier fill**, which is a different question rather than a
variation. The bucket spreads across one colour and stops where that colour stops; the barrier
spreads across everything and stops only where it meets *your* colour. Draw an outline, drop
the barrier inside it, and it fills up to the outline whatever is in there — where a bucket
would refuse to cross a region of mixed shades. The word `barrier` appears beside the pointer
while `SHIFT` is held, because the two look identical until one of them runs.

**`SHIFT` + wheel sizes the tip, and each tool remembers its own.** An eraser wants to be
twenty across and a pencil wants to be one, so they are not the same number. The step is per
tool too: one pixel at a time for the pencil, line, rect and ellipse, because a pixel is what
they are for, and 5 or 3 for the eraser and the spray, which would otherwise take twenty
notches to get anywhere.

The small tips are drawn by hand rather than computed. A circle of radius 2 or 3 from a
formula is a lopsided smudge; sizes 2 to 5 are bitmaps placed by eye, and size 1 is exactly
one pixel.

There is no tool window. Each tool shows itself three ways, none of them a panel: the
**cursor** is a crosshair over the sheet; the **tip is outlined** where it would land, so its
size and shape are visible before it is used; and a **line-art glyph hangs up and to the
right of the pointer** saying which tool it is. The glyph is an outline, not a filled icon,
so the artwork shows through the middle of it, and it hangs clear of the aim point so it
never stands on the pixel it is reporting about — both of those are the first Vangopix's
answers, carried over vertex for vertex.

A stroke joins its samples, so a fast hand draws a line and not a row of dots. One stroke is
one undo.

### The 1:1 panel

`V` puts it in the bottom-right corner and `V` takes it away. **It is not a miniature** — it
shows the document at one art pixel to one screen pixel, cropped around whatever you are
looking at. Pixel art is drawn at 800% and looked at at 100%, and this answers the question
you ask twenty times an hour: what does this actually look like?

When the document is smaller than the panel, the panel shrinks to the document — so your
sprite *is* the panel, at the size it will be seen. When it is bigger, hovering the panel
outlines the part of the sheet it is showing.

(With a selection in hand, `V` flips it instead — same arrangement `R` has with the ellipse.)

### The selection

`Z` takes it. Drag a rectangle, then drag again from inside it to pick the pixels up and
carry them; click away to put them down. `CTRL+C`, `CTRL+X` and `CTRL+V` copy, cut and paste,
and the clipboard crosses between documents — copy in one tab, switch, paste in another.
`CTRL+A` takes the whole sheet, `DELETE` clears the selection, `ESC` lets it go.

While something is selected, `V` and `H` flip it, `I` inverts its colours and `R` turns it a
quarter clockwise. Those are the only bare keys in the program that mean something different
depending on state — allowed because both halves of the condition are on screen: the select
tool is in hand and a rectangle is marked. With nothing selected, `R` is the ellipse again.

Dragging a selection **moves** it, and what it leaves behind is **colour 2** — nothing by
default, so it looks like a hole, but load colour 2 with white and a cut leaves paper. Hold
`CTRL` while dragging and it is **copied** instead: the place it came from is untouched.
`CTRL` anywhere outside the selection is still the eyedropper.

**Nothing is written to the sheet while a selection floats.** The hole where it came from is
*drawn*, not dug, so letting go of a float costs nothing and leaves nothing to undo. Putting
one down is a single undo step — the old place emptied and the new place written together,
which is what makes nudging a selection by one pixel come out right.

### Two colours, one per mouse button

Left button draws colour 1, right button draws colour 2. **`CTRL` + click absorbs the colour
under the pointer** into whichever slot that button owns — so the button that takes a colour
is the button that lays it down. Keep the button down and drag to go on absorbing.

Colour 2 starts as **nothing**, which is what makes the right button an eraser without an
eraser existing: in a program that keeps alpha, rubbing out *is* drawing with nothing. Put a
colour in slot 2 and the right button draws with it instead.

`SHIFT`+`R` rolls a random colour into slot 1, and `M` replaces slot 1 with the average of
the two — alpha included, which is how you make a half transparent shade without a slider.
Both are for when the colour you want is *near* one you already have.

While `CTRL` is held, a bar follows the pointer with the colour under it and its value as
`RRGGBBAA` — what you would get if you pressed. The two loaded colours sit at the bottom
left, side by side, and step aside when the project sidebar slides in.

That is the whole colour interface for now, and it is deliberate: a pixel artist settles on
eight to sixteen colours and then picks from the drawing constantly, so **the image is the
palette**. A window of coloured tiles is for colours that are not on the sheet yet, and it
will be summoned like everything else here rather than parked down the side.

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
  tool.c/.h     the pencil: the only file that changes a pixel
  glyph.c/.h    the line-art glyphs
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
