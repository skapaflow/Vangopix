# Vangopix

A pixel art editor. The name is Van Gogh plus pixel.

Vangopix takes Paint as its starting point and walks toward Aseprite, but with one rule
held above the others: **the minimalism is in the interface, not in the features.**
There is no docked panel, no permanent toolbar, no strip of screen reserved for chrome.
The sheet fills the window. Anything that needs a window appears on demand and leaves
when it is done.

That is the complaint this project answers. Aseprite reserves a small rectangle in the
middle for drawing and surrounds it with tools; Vangopix reserves nothing.

## Status

Early. The window, the sheet, and the tab model exist. Drawing does not yet.

## Building

Windows, mingw-w64 (or clang in gcc-compatible mode) and GNU make.

| dependency | expected at | override |
| --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL/releases) 3.4+ (mingw devel package) | `C:/SDL3` | `make SDL3=...` |
| [SDL3_image](https://github.com/libsdl-org/SDL_image/releases) 3.4+ (mingw devel package) | `C:/SDL3_image` | `make SDL3IMG=...` |

```sh
mingw32-make          # build and run
mingw32-make release  # build without the console window
mingw32-make dll      # refresh the runtime dlls next to the exe
```

`SDL3.dll` and `SDL3_image.dll` must sit next to `vangopix.exe`; `make dll` copies them.
The `libpng16` / `libtiff` / `libwebp` / `libavif` dlls from the SDL3_image runtime
package are optional in the literal sense — SDL3_image loads them by name when it meets
a file of that format and runs fine without them. Each one you drop in adds formats.

SDL3 3.4 reads and writes png in core, so the satellite is not strictly required for
pixel art. It is linked because Vangopix is meant to open anything: webp, avif, tiff,
gif, tga, qoi, ico, pcx, svg, xcf.

## Keys

| | |
| --- | --- |
| `CTRL+N` | new sheet in a new tab |
| `CTRL+W` | close the tab |
| `CTRL+TAB` / `CTRL+SHIFT+TAB` | walk through the tabs |
| drag a file in | opens it in a new tab |

Files named on the command line each open in their own tab, so Vangopix can be
associated with an image extension and handed a whole selection at once.

## Layout

```
src/
  vangopix.h   the four objects every module needs to name
  main.c       the frame envelope: window, loop, present
  tabs.c/.h    the tabs, which ARE the documents
icon/          the .ico and the .rc that puts it on the exe
```

One file per responsibility, never per size.
