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

Early. The window, the sheet, the tab model and text rendering exist. Drawing does not
yet.

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
| `CTRL+N` | new sheet in a new tab |
| `CTRL+W` | close the tab |
| `CTRL+TAB` / `CTRL+SHIFT+TAB` | walk through the tabs |
| `TAB` | show and hide the tab bar |
| `F1` | toggle the document overlay |
| wheel | zoom in steps, centred on the cursor |
| middle drag, or `SPACE` + left drag | pan |
| `CTRL+0` / `CTRL+1` | fit the sheet / go to 1:1 |
| drag a corner grip | resize the canvas |
| `SHIFT` while dragging | put the corner on the 8 pixel grid |

The four small blue squares sit just outside the corners of the sheet, touching it only
at the corner point so they never cover the artwork. Dragging one moves that corner and
leaves the opposite one anchored; the outline and the size readout follow the hand, and
the pixels are only rebuilt on release.

Holding `SHIFT` lands the dragged corner on the nearest multiple of eight - it does not
step by eight, so a canvas of 53 snaps straight to 56 however the drag arrived there.
The readout shows `[8]` while it is on.

While the bar is up: click a tab to select it, drag it sideways to reorder, click its
`x` to close it, click `+` for a new sheet. The bar floats over the sheet and takes no
space when it is down.
| drag a file in | opens it in a new tab |

Files named on the command line each open in their own tab, so Vangopix can be
associated with an image extension and handed a whole selection at once.

## Layout

```
src/
  main.c        the pipeline, and nothing else
  vangopix.c/.h the program: globals, window, renderer, font, argv
  core.c/.h     the frame: input, draw, present
  tabs.c/.h     the tabs, which ARE the documents
  tabbar.c/.h   the tab bar: the only file that draws a tab
  view.c/.h     the camera: stepped zoom at the cursor, and pan
  resize.c/.h   the corner grips that resize the canvas
  text.c/.h     glyphs packed into one atlas
```

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
