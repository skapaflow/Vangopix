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
    # cmd.exe has no `mkdir -p`, and `mkdir` on an existing directory is an ERROR here -
    # which would fail the build on every run after the first.
    MKBUILD    = if not exist $(OBJDIR) mkdir $(OBJDIR)
    RMBUILD    = if exist $(OBJDIR) rmdir /S /Q $(OBJDIR)
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
    MKBUILD    = mkdir -p $(OBJDIR)
    RMBUILD    = rm -rf $(OBJDIR)
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
#   thumb.c    the 1:1 panel: what the art looks like at real size
#   win.c      the window frame: move, stretch, raise, close - not a toolkit
#   colour.c   the colour window: where a new colour comes from
#   palette.c  the palette: one list of colours, shown as a window and under ALT
#   primitives.c the shapes SDL does not draw, and the ones it draws loosely
#   ui.c       the measurements that come from the face, so a layout follows it
#   field.c    one line of text inside a window - the first widget win.c grew
#   expr.c     a line of arithmetic, answered - what a number box means by 32*4
#   keymap.c   what the keys are, and the keyboard.txt a person can edit
#   splash.c   the picture the program opens with, and the one place it is named
#   anim.c     sprite animation: a clip is a region of the sheet, not a buffer
#   glyph.c    the line-art glyphs, drawn as wireframe paths
#   text.c     glyphs packed into one atlas by stb_truetype
OBJDIR = build
SRC = src/main.c src/vangopix.c src/core.c src/keys.c src/tabs.c src/tabbar.c src/view.c src/resize.c src/project.c src/sidebar.c src/prompt.c src/file.c src/undo.c src/tool.c src/select.c src/thumb.c src/win.c src/colour.c src/palette.c src/primitives.c src/ui.c src/field.c src/expr.c src/keymap.c src/splash.c src/anim.c src/glyph.c src/text.c
DEP = $(wildcard src/*.h)
OBJ = $(SRC:src/%.c=$(OBJDIR)/%.o)

all: $(OUT) run

#
# ONE OBJECT PER SOURCE, SO A ONE FILE EDIT COMPILES ONE FILE. Twenty translation units
# rebuilt for a comment in colour.c is a wait that gets paid on every keystroke of the day,
# and it is the reason a build stops being run often enough to catch things early.
#
# They go in build/ rather than beside the sources: src/ holds what was written, and a
# directory that mixes the two makes `ls` useless and one stray *.o in a commit likely.
# `make clean` throws the whole directory away, which is also the only honest way to force
# a full rebuild.
#
# $(DEP) - every header - is on EVERY object rather than the real per-file dependency. It
# is coarse: touching one header rebuilds all twenty, exactly as before. But the failure it
# prevents is the one worth being crude about - two translation units holding different
# views of the same struct, which is silent corruption surfacing far from its cause. The
# precise answer is the compiler's own -MMD depfiles, and that is a change to make when
# the header set is big enough to feel it.
#
$(OBJDIR)/%.o: src/%.c $(DEP) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@$(MKBUILD)

$(OUT): $(OBJ) $(RES)
	$(CC) $(OBJ) $(RES) -o $(OUT) $(LFLAGS)

run: $(OUT)
	./$(OUT)

# The parts of this program with an answer that is right or wrong without a hand on the
# mouse: a resize undone has to give back the pixels a shrink destroyed, and a pencil
# stroke has to join its samples instead of coming out dotted. It links the same sources
# minus main.c, opens a hidden window because a document owns a texture, and prints PASS
# or FAIL per claim.
#
# NOT part of `all`: a build should not open a window every time it succeeds.
# It links THE SAME OBJECTS the program does, minus main.o - so a suite that passes has
# tested the exact code that shipped, and running `make` after `make test` recompiles
# nothing. main.o is the only one left out: two mains do not link.
TEST_OBJ = $(filter-out $(OBJDIR)/main.o,$(OBJ))

$(OBJDIR)/checks.o: test/checks.c $(DEP) | $(OBJDIR)
	$(CC) $(CFLAGS) -c test/checks.c -o $@

test: $(OBJDIR)/checks.o $(TEST_OBJ)
	$(CC) $(OBJDIR)/checks.o $(TEST_OBJ) -o $(TEST_OUT) $(LFLAGS)
	./$(TEST_OUT)

# -mwindows drops the console, and only on Windows does it mean anything. It is now on by
# DEFAULT: a black terminal opening behind an image editor is not a thing to ship, and this
# program stopped being a thing that is only ever run from a shell.
#
# `make CONSOLE=1` puts it back, which is the half worth keeping - SDL_Log is the only window
# into a failure that does not stop the program, and a mute editor is worse than an ugly one
# while something is being chased. Nothing is removed for the console build; it is one word.
ifeq ($(OS),Windows_NT)
ifndef CONSOLE
    LFLAGS += -mwindows
endif
endif

release: CFLAGS += -DNDEBUG
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
	@$(RMBUILD)

else

dll:
	@echo "dll: nothing to do - shared libraries come from the package manager here"

clean:
	rm -f $(OUT) $(TEST_OUT)
	$(RMBUILD)

endif

# `test` is also the name of a DIRECTORY. Without it on this list make finds the
# directory, decides the target is up to date and runs nothing - reporting success for a
# suite it never built.
.PHONY: all clean run release dll test
