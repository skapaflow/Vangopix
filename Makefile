SHELL = cmd.exe

# Vangopix - a pixel art editor.
# Library paths. Override: make SDL3=/other/path SDL3IMG=/other/path
SDL3    ?= C:/SDL3
SDL3IMG ?= C:/SDL3_image

CC     = gcc
CFLAGS = -Wall -Wextra -O2 -I$(SDL3)/include -I$(SDL3IMG)/include -Isrc
LFLAGS = -L$(SDL3)/lib -L$(SDL3IMG)/lib -lSDL3 -lSDL3_image

# SDL3 3.4+ reads and writes png on its own (SDL_LoadPNG / SDL_SavePNG). SDL3_image is
# linked anyway, and for a PRODUCT reason rather than a technical one: Vangopix is meant
# to become this machine's default viewer, the place where any image opens. A reader
# that only understands png does not replace Paint. The satellite brings webp, avif,
# tiff, gif, tga, qoi, ico, pcx, svg, xcf - plus IMG_LoadAnimation / IMG_SaveAnimation,
# which is the road to animated gif once a timeline exists.

# One file per RESPONSIBILITY, never per size.
#   main.c = the frame envelope: window, loop, present
#   tabs.c = the tabs, which ARE the documents (sheet, view, and one day undo, palette)
#
# To come:
#   view.c = the camera: integer zoom and pan (what the eye sees, not what exists)
#   tool.c = the tools: pencil, bucket, eyedropper
#   io.c   = open and save
SRC = src/main.c src/tabs.c
DEP = $(wildcard src/*.h)
OUT = vangopix.exe

# Windows resource: the exe/window icon plus VERSIONINFO. It joins the LINK as if it
# were an object file - it is not a header and not a library, it is a COFF blob the
# linker appends to the binary.
RES = icon/recicon.res

all: $(OUT) run

# $(DEP) belongs on the list: without it, touching a header recompiles nothing and the
# translation units end up with different views of the same struct - silent corruption
# from an ABI mismatch, surfacing far from its cause.
$(OUT): $(SRC) $(DEP) $(RES)
	$(CC) $(CFLAGS) $(SRC) $(RES) -o $(OUT) $(LFLAGS)

# The .res is committed, but as a RULE rather than a comment: editing recicon.rc
# (new icon, bumped version) has to rebuild it on its own. Parking the windres line in
# a separate file leaves the manual step waiting to be forgotten - and an exe with the
# old icon reports no error at all.
$(RES): icon/recicon.rc icon/vangopix.ico
	windres -i icon/recicon.rc --input-format=rc --target=pe-x86-64 -o $(RES) -O coff

run: $(OUT)
	$(OUT)

# -mwindows drops the console. It stays OUT of the normal build on purpose: while
# developing, SDL_Log is the only window into the program, and a mute editor is worse
# than an ugly one. Only the release hides it.
release: CFLAGS += -mwindows -DNDEBUG
release: clean $(OUT)

# The dlls have to sit next to the exe. This target exists for the day you upgrade SDL
# and forget to refresh the local copy.
#
# The libpng16 / libtiff / libwebp / libavif dlls in the root do NOT belong here: they
# come from the SDL3_image runtime package and are optional in the literal sense -
# SDL3_image.dll calls LoadLibrary on them by name when it meets a file of that format,
# and survives their absence. Deleting libavif-16.dll breaks nothing; it only stops
# .avif from opening.
dll:
	copy /Y "$(subst /,\,$(SDL3))\bin\SDL3.dll" SDL3.dll
	copy /Y "$(subst /,\,$(SDL3IMG))\bin\SDL3_image.dll" SDL3_image.dll

clean:
	@if exist $(OUT) del $(OUT)

.PHONY: all clean run release dll
