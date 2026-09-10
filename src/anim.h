#ifndef VANGOPIX_ANIM_H
#define VANGOPIX_ANIM_H

#include "vangopix.h"
#include "tabs.h"

/*
 * SPRITE ANIMATION, AND THE FIRST THING TO SAY IS WHAT IT IS NOT.
 *
 * THERE ARE NO FRAME BUFFERS. A document is still one sheet, and VNG_TAB is untouched by any
 * of this. That is not a simplification of the first Vangopix - it is what the first Vangopix
 * does. Its VANGO_IMAGE has no frame list, no frame count, no delay and no current-frame
 * index; grep the whole of src/image/ for "frame" and nothing comes back.
 *
 * WHAT IT HAS INSTEAD IS A CLIP: a rectangle on the canvas plus a count.
 *
 *     typedef struct { char name[32]; float speed; int frames; int x, y, w, h; } ANIME_SET;
 *
 * Frame n is that rectangle shifted right by n*w, blitted straight out of the canvas texture.
 * It is SPRITESHEET-REGION PLAYBACK, which is how sprite sheets are actually drawn: you lay
 * the poses out in a row and the animation is a window sliding along them. Nothing about it is
 * ever stored in an image file; only the rectangles go into a .anime sidecar.
 *
 * That is why this module changes nothing about the document, the undo stack or saving. It
 * reads regions of the sheet that already exists - which is exactly what thumb.c does, with a
 * source rect on t->tex.
 *
 * `vert` is the ROW, and it is the other half of the idea: a sheet holds one clip per row, so
 * the same rectangle stepped down by h picks the walk instead of the run.
 *
 * ---------------------------------------------------------------------------------------
 *
 * TWO WINDOWS, WHICH IS THE ORIGINAL'S ARRANGEMENT:
 *
 *   the player   150 x 120, resizable. The preview, the row step, the file icons and the
 *                list of clips. Summoned by X - the first Vangopix's own KB_TOOL_ANIMATION.
 *   the editor   150 x 200, fixed. Seven boxes - n s f x y w h - and a Create button. It is
 *                the first window in this program that genuinely IS a form, and it is why
 *                field.c exists.
 *
 * AND BEING A FORM RATHER THAN A BOX IS A BEHAVIOUR, not a description. field.h leaves the
 * question to the owner because a lone box and a form of seven mean different things by
 * leaving a box: the colour window's hex field is alone, so a click elsewhere is a mis-click
 * and dropping what was half-typed is the kind reading. Here the click that leaves a box is
 * the click that MOVES TO THE NEXT ONE - or the one that presses Create, which is the button
 * that means "take this" - so leaving a box commits it, by any route, including a press over
 * in the player. Only ENTER committing meant filling in all seven boxes and pressing Create
 * produced the clip the editor opened with: no name, 32 x 32, AND NO FRAMES. That last is why
 * it read as two faults - a clip with no frames draws neither the preview nor the grid, so the
 * window that would not keep the numbers was also the window showing nothing.
 *
 * Each box also OPENS HOLDING WHAT IT WAS READING OUT. They are the first fields in the
 * program to report a value they do not own, and they opened from their own buffer, which is
 * empty until somebody has typed into that box once - so clicking the width to change it
 * blanked the width first.
 *
 * WHAT IS FIXED RATHER THAN CARRIED OVER. The design is good and the implementation was not;
 * every one of these is a real defect in gui_animation.c, and each is named again at the line
 * that fixes it:
 *
 *   - PLAYBACK WAS MEASURED IN RENDERED FRAMES. `fps += anime_box.speed` once per draw, so
 *     the animation ran at a speed that depended on the monitor. vng_dt exists for this, and
 *     the precedent is the spray, which is measured in time for the same reason;
 *   - THE PLAYHEAD RAN ONE PAST THE END. `fps > (frames - (int)speed) ? 0 : fps` lets fps
 *     reach 4.0 on a four-frame clip, so the fifth cell - whatever is to the right on the
 *     sheet - was blitted for one tick, every loop;
 *   - DELETE AND MOVE-UP USED THE SCREEN ROW while selection used the scrolled index, so the
 *     moment the list was scrolled they acted on the wrong clip;
 *   - `new_frame` WAS CLEARED ONLY ON COMMIT, so closing the editor with its close box left
 *     it set and the next edit appended a duplicate instead of updating;
 *   - THE PARSER HAD NO BOUNDS ANYWHERE. `sscanf("\"%[^\"]\"")` with no width into a 32-byte
 *     name; no limit on the line count against a 256-entry array; `list_lot++` on append
 *     never clamped; `fclose(NULL)` on any failed open, twice; sscanf's return ignored, so a
 *     malformed line still counted;
 *   - `list_lot == 0` REACHED A DIVISION in the scrollbar - float 0/0, then an undefined
 *     conversion to int;
 *   - THE ROW INDEX HAD NO UPPER CLAMP, so it walked off the bottom of the sheet for ever
 *     with nothing on screen to say how many rows there were.
 *
 * AND TWO THINGS THAT GO BLANK WITHOUT SAYING SO, which are neither the original's nor the
 * form's - they are what a preview drawn from a texture does when nobody asks the awkward
 * question:
 *
 *   - THE FRAME MAY NOT BE ON THE SHEET. A clip is a rectangle stepped RIGHT by n*w, so four
 *     frames of 32 starting at x=32 want a sheet 160 wide, and pixel art is 64.
 *     SDL_RenderTexture with a source rect off the texture draws nothing and RETURNS TRUE -
 *     no error, no log, an empty box for three quarters of every loop. Half off is worse: SDL
 *     clamps the sampling and stretches the edge column into a picture that is nowhere on the
 *     sheet. So a frame is wholly on the sheet or it is not drawn, and the preview's rim goes
 *     red to say which;
 *   - THE PREVIEW SITS ABOVE THE WINDOW, and win.c keeps the head bar on screen, not the room
 *     a preview wants over it. Summoned near the top edge - or zoomed up sixteen times - it
 *     went off the top and the player looked like it was playing nothing. It flips below when
 *     there is no room above.
 */

/* How many clips a .anime may hold, and how long a name may be. Both are stated because the
   original stated neither and wrote past both. */
#define VNG_ANIM_MAX   64
#define VNG_ANIM_NAME  32

extern void anim_toggle  (void);
extern bool anim_visible (void);

/*
 * The grid on the SHEET saying where the frames are - the one thing this module draws outside
 * its own window, and the same split thumb.c makes: win_draw draws the panel, this draws the
 * marker. Called in the sheet band of the frame.
 */
extern void anim_draw (VNG_TAB *t);

extern void anim_free (void);

/* The clock, once a frame - a window that is not showing is not playing. */
extern void anim_tick (void);

/* ------------------------------------------------------------------ pinned by the checks */

/*
 * Which frame is showing after `t` seconds of a clip of `frames` at `speed` seconds each.
 *
 * Split out and exposed because it is where the original was wrong twice over - once in the
 * unit and once in the bound - and because neither mistake is visible in a build log. It has
 * to be right for every t, and a check can say so without a window.
 */
extern int anim_frame_at (float t, int frames, float speed);

/*
 * Reads a .anime into the clip list and returns how many came in.
 *
 * It REPLACES the list on success and LEAVES IT ALONE on failure - losing the clips in hand
 * because a path was mistyped is the worse of the two answers, and the original did neither
 * deliberately: it called fclose on a NULL handle and crashed.
 *
 * A line that is not a clip is skipped rather than counted, and that includes a name longer
 * than the field: the width in the scanf is what stops it spilling into the rest of the
 * struct, and a line it cannot read whole is a line it refuses. The editor's own field caps
 * at the same length, so only a hand-edited file can produce one.
 *
 * Exposed for the same reason anim_frame_at is: the parser is where the original wrote off
 * the end of three separate things.
 */
extern int anim_load (const char *path);
extern int anim_lot  (void);

/* Name of clip i, or "" - never off the end. */
extern const char *anim_name (int i);

#endif
