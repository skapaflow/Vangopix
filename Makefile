# Vangopix - a pixel art editor.
#
# One makefile for Windows, Linux and macOS. The platform decides three things and no
# more: where the SDL headers come from, what the binary is called, and whether there is
# an icon resource to link. Everything below that line is shared.

ifeq ($(OS),Windows_NT)
    # cmd.exe on purpose: with git-bash installed, make would otherwise find sh.exe and
    # the del/copy recipes below would break in a way that depends on who is compiling.
    SHELL      = cmd.exe
    OUT        = vangopix.exe
    TEST_OUT   = checks.exe
    SDL3      ?= C:/SDL3
    SDL3IMG   ?= C:/SDL3_image
    SDL_CFLAGS = -I$(SDL3)/include -I$(SDL3IMG)/include
    SDL_LIBS   = -L$(SDL3)/lib -L$(SDL3IMG)/lib -lSDL3 -lSDL3_image
    # Windows resource: the exe/window icon plus VERSIONINFO. It joins the LINK as if it
    # were an object file - not a header, not a library, a COFF blob the linker appends.
    RES        = icon/recicon.res
else
    OUT        = vangopix
    TEST_OUT   = checks
    # pkg-config rather than hardcoded paths: on Linux and macOS the libraries come from
    # a package manager that already knows where it put them, and hardcoding would be
    # wrong on every distribution and on both homebrew prefixes.
    SDL_CFLAGS = $(shell pkg-config --cflags sdl3 sdl3-image)
    SDL_LIBS   = $(shell pkg-config --libs sdl3 sdl3-image) -lm
    RES        =
endif

# ?= would NOT work here: make ships a built-in CC = cc, which counts as set, so the
# assignment would never fire and a machine without cc would fail. Testing the origin
# overrides only make own default and still yields to the environment and the command
# line (make CC=clang).
ifeq ($(origin CC),default)
    CC = gcc
endif
CFLAGS  = -Wall -Wextra -O2 -Isrc $(SDL_CFLAGS)
LFLAGS  = $(SDL_LIBS)

# One file per RESPONSIBILITY, never per size.
#   main.c     the pipeline, and nothing else
#   vangopix.c the program: globals, window, renderer, font, argv
#   core.c     the frame: input, draw, present
#   keys.c     who has the keyboard - one owner, and the gate on the polled state
#   prompt.c   one line of text, asked for and gone
#   tabs.c     the tabs, which ARE the documents
#   tabbar.c   the tab bar: the only file that draws a tab
#   view.c     the camera: stepped zoom at the cursor, and pan
#   resize.c   the corner grips that resize the canvas
#   project.c  the project folders and projects.vngproj (the model)
#   sidebar.c  the project panel (the pixels)
#   file.c     save, and the two system dialogs that go with it
#   undo.c     the undo stack: pixel carries and resizes, per document
#   tool.c     the pencil: the only file that changes a pixel
#   select.c   the selection: marked or floating, and nothing in between
#   glyph.c    the line-art glyphs, drawn as wireframe paths
#   text.c     glyphs packed into one atlas by stb_truetype
SRC = src/main.c src/vangopix.c src/core.c src/keys.c src/tabs.c src/tabbar.c src/view.c src/resize.c src/project.c src/sidebar.c src/prompt.c src/file.c src/undo.c src/tool.c src/select.c src/glyph.c src/text.c
DEP = $(wildcard src/*.h)

all: $(OUT) run

# $(DEP) belongs on the list: without it, touching a header recompiles nothing and the
# translation units end up with different views of the same struct - silent corruption
# from an ABI mismatch, surfacing far from its cause.
$(OUT): $(SRC) $(DEP) $(RES)
	$(CC) $(CFLAGS) $(SRC) $(RES) -o $(OUT) $(LFLAGS)

run: $(OUT)
	./$(OUT)

# The parts of this program with an answer that is right or wrong without a hand on the
# mouse: a resize undone has to give back the pixels a shrink destroyed, and a pencil
# stroke has to join its samples instead of coming out dotted. It links the same sources
# minus main.c, opens a hidden window because a document owns a texture, and prints PASS
# or FAIL per claim.
#
# NOT part of `all`: a build should not open a window every time it succeeds.
TEST_SRC = $(filter-out src/main.c,$(SRC))
test: test/checks.c $(TEST_SRC) $(DEP)
	$(CC) $(CFLAGS) test/checks.c $(TEST_SRC) -o $(TEST_OUT) $(LFLAGS)
	./$(TEST_OUT)

# -mwindows drops the console, and only on Windows does that mean anything. It stays OUT
# of the normal build on purpose: while developing, SDL_Log is the only window into the
# program, and a mute editor is worse than an ugly one.
release: CFLAGS += -DNDEBUG
ifeq ($(OS),Windows_NT)
release: CFLAGS += -mwindows
endif
release: clean $(OUT)

ifeq ($(OS),Windows_NT)

# The .res is derived, not authored: the sources are icon/recicon.rc and the .ico beside
# it. As a RULE rather than a comment, editing either one rebuilds the binary on its own
# - a windres line parked in a text file leaves a manual step waiting to be forgotten,
# and an exe carrying last week's icon reports no error at all.
$(RES): icon/recicon.rc icon/vangopix.ico
	windres -i icon/recicon.rc --input-format=rc --target=pe-x86-64 -o $(RES) -O coff

# The dlls have to sit next to the exe, and only Windows works that way. The
# libpng16 / libtiff / libwebp / libavif dlls in the root are NOT copied here: they come
# from the SDL3_image runtime package and are optional in the literal sense -
# SDL3_image.dll calls LoadLibrary on them by name when it meets a file of that format
# and survives their absence. Deleting libavif-16.dll breaks nothing; it only stops
# .avif from opening.
dll:
	copy /Y "$(subst /,\,$(SDL3))\bin\SDL3.dll" SDL3.dll
	copy /Y "$(subst /,\,$(SDL3IMG))\bin\SDL3_image.dll" SDL3_image.dll

clean:
	@if exist $(OUT) del $(OUT)
	@if exist $(TEST_OUT) del $(TEST_OUT)
	@if exist $(subst /,\,$(RES)) del $(subst /,\,$(RES))

else

dll:
	@echo "dll: nothing to do - shared libraries come from the package manager here"

clean:
	rm -f $(OUT) $(TEST_OUT)

endif

# `test` is also the name of a DIRECTORY. Without it on this list make finds the
# directory, decides the target is up to date and runs nothing - reporting success for a
# suite it never built.
.PHONY: all clean run release dll test
