# Vangopix

A pixel art editor. The name is Van Gogh plus pixel.

Vangopix takes Paint as its starting point and walks toward Aseprite, with one idea held
above the others: **the minimalism is in the interface, not in the features.** There is no
docked panel, no permanent toolbar, no strip of screen reserved for chrome. The sheet
fills the window, and anything that needs a window appears on demand and leaves when it is
done.

That is the complaint this project answers. Aseprite reserves a small rectangle in the
middle for drawing and surrounds it with tools; Vangopix reserves nothing.

## Status

Early, but **it draws.** Nine tools, undo behind them, and everything an image gets around
that — opening, viewing, framing, sizing and saving.

| | |
| --- | --- |
| drawing | nine tools on `Q W E R / A S D F / Z`, each with its own tip size |
| selection | mark, move, copy and paste between documents, flip, rotate, invert |
| undo | per document, made of pixel carries and resizes rather than snapshots |
| colour | two, one per mouse button, filled by `CTRL`+click; a hue-ring picker and a palette |
| documents | tabs that carry their own camera, reorderable by dragging |
| projects | folders in a sidebar, listing only images, with a preview under the pointer |
| camera | stepped zoom at the cursor, pan, and one key back to 1:1 |
| canvas | resize by the corner grips, with an optional 8 pixel grid |
| files | open and save through the system's own dialogs; the format follows the extension |
| formats | anything SDL3_image reads: png, webp, avif, tiff, gif, jpg, tga, qoi, ico, pcx, svg, xcf |
| animation | a clip is a region of the sheet, played from it and kept beside the image |
| keys | remappable in `keyboard.txt`, written beside the exe from the defaults |
| transparency | checkerboard behind the sheet, black frame around it |

It opens on an **empty desk** with the keys written across it — there is no menu to read,
so that is where the program says what it does. `F1` brings the list back later, behind
the artwork.

## Building

C99 and SDL3. One makefile covers Windows, Linux and macOS.

| dependency | Windows | Linux / macOS |
| --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL/releases) 3.4+ | `C:/SDL3`, or `make SDL3=...` | `pkg-config sdl3` |
| [SDL3_image](https://github.com/libsdl-org/SDL_image/releases) 3.4+ | `C:/SDL3_image`, or `make SDL3IMG=...` | `pkg-config sdl3-image` |

```sh
make            # build and run
make test       # the parts that can be checked without a hand on the mouse
make release    # build without the console window
make icon       # Windows only: the exe icon resource, from the .rc and the .ico
make dll        # Windows only: refresh the runtime dlls beside the exe
```

On Windows `SDL3.dll` and `SDL3_image.dll` must sit next to `vangopix.exe`. The
`libpng16` / `libtiff` / `libwebp` / `libavif` dlls from the SDL3_image runtime package
are optional in the literal sense — SDL3_image loads them by name when it meets a file of
that format and runs fine without them. Each one you add adds formats.

SDL3 3.4 reads and writes png in core, so the satellite is not strictly required for pixel
art. It is linked because Vangopix is meant to open anything.

## Keys

Everything on one key with no modifier can be moved: `keyboard.txt` sits beside the
executable, holds `action = KEY`, and is written again from the defaults whenever it is
deleted. It cannot reword what a key does, only move it — so the list on screen and the
program cannot disagree.

| | |
| --- | --- |
| `CTRL+N` | new sheet — asks for `w x h` |
| `CTRL+O` | open one or more files |
| `CTRL+S` / `CTRL+SHIFT+S` | save / save as |
| `CTRL+W` | close the sheet — asks if that would lose work |
| `CTRL+Z` / `CTRL+Y` | undo / redo |
| `1` `2` | the sheet before, the one after |
| `0` | put it back: centred, one pixel to one |
| `Q` `W` `E` `R` | pencil, line, rect, ellipse |
| `A` `S` `D` `F` | eraser, bucket, spray, change-colours |
| `Z` | select |
| `TAB` | projects |
| `ESC` | the row of sheets |
| `C` `P` `V` `X` | colour, palette, 1:1 panel, animation |
| `F1` | the keys, and what is on screen |
| wheel | zoom in steps, centred on the cursor |
| middle drag, or `SPACE` + left drag | pan |
| `SHIFT` + wheel | tip size, per tool |
| left / right drag | draw colour 1 / colour 2 |
| `CTRL` + left / right click | absorb that colour into 1 or 2; drag to keep absorbing |
| `ALT` | the palette under the pointer |
| `SHIFT`+`R` / `M` | a random colour into slot 1 / average the two into slot 1 |
| `CTRL+A` `CTRL+C` `CTRL+X` `CTRL+V` | select all, copy, cut, paste |
| `V` `H` `I` `R`, with a selection | flip, mirror, invert, rotate |
| `DELETE` / `ESC`, with a selection | clear it / let it go |
| `CTRL` + drag a selection | copy it instead of moving it |
| `SHIFT` + click, pencil | draw a straight line from the last point |
| `SHIFT` + drag a line | snap to the pixel-art slopes: 2:1, 1:1, 1:2 |
| `SHIFT` + bucket | barrier fill — spread until it meets your colour |
| `SHIFT` + drag a grip | put the canvas corner on the 8 pixel grid |
| drag a file in | open it in a new tab |
| drag a folder in | add it as a project |
| `ENTER` / `ESC` in a field | accept / cancel |

Number boxes take arithmetic: type `32*4` for a sprite offset and the box hands over
`128`.

Files named on the command line each open in their own tab, so Vangopix can be associated
with an image extension and handed a whole selection at once.

## Fonts

The program looks beside the executable for `font/DejaVuSansMono.ttf`, and **runs without
text if it is not there** — that is a supported state, not a crash: drawing a letter is a
no-op when no face loaded, and the check suite runs that way on purpose.

**DejaVu Sans Mono** ships with the repository under the Bitstream Vera / DejaVu licence,
which permits redistribution and asks that the notice travel with the font —
`font/LICENSE_DEJAVU.txt`. It is monospaced, which matters beyond taste: anything laid out
in columns needs one fixed character cell, and a proportional face makes every column
drift.

## Credits

Written by Alberto F. Jr.

Vangopix is the second of its name. The first was written without AI assistance and is the
source of most of the design that survives here. This one is written with
[Claude Code](https://claude.com/claude-code) (Claude Opus 5) as a collaborator, and that
participation is recorded in the commit history rather than hidden.

Text rendering and the camera are adapted from the author's Skyonara engine, the latter
carrying pan and zoom that go back to the 2016 Skyonara. Text sits on top of
[stb_truetype](https://github.com/nothings/stb) by Sean Barrett (public domain).
