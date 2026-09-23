#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 202405L
#endif

#include <sys/mman.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fcft/fcft.h>

#include "anvl.h"
#include "bar.h"
#include "config.h"
#include "river.h"

#define MIN(A, B) (A < B ? A : B)
#define MAX(A, B) (A > B ? A : B)
#define LENGTH(A) (sizeof A / sizeof A[0])
#define ISVISIBLE(C) (C->tagmask & C->mon->tagmask)
#define CLAMP(VAL, MIN, MAX) VAL = VAL < MIN ? MIN : (VAL > MAX ? MAX : VAL)

// TODO: reconsider how windows are treated here
// Used for dragging
// Used for dragging

/*
 * Register a new window with the window manager.
 *
 * New windows are placed on the selected output and its currently active tag.
 * The new window also becomes the focused window for all seats.
 */
void anvl_add_window(Window* window) {
    window->mon = selmon;
    window->tag = selmon->seltag;

    wl_list_insert(&anvl.windows, &window->link);

    Tag* tag = selmon->tags[selmon->seltag];
    tag->focused = window;

    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) {
        seat->focused = window;
    }
}

/*
 * Remove a window from the window manager.
 *
 * Any seat currently focusing this window has its focus cleared. The window
 * is then removed from the global window list.
 */
void anvl_remove_window(Window* window) {
    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) {
        if (seat->focused == window)
            seat->focused = NULL;
    }

    if (window->mon != NULL) {
        Tag* tag = window->mon->tags[window->tag];

        if (tag->focused == window)
            tag->focused = NULL;
    }

    wl_list_remove(&window->link);
}

/*
 * Register a newly discovered output.
 *
 * Each output owns its own set of tags. All tags initially use the default
 * layout and contain no focused window.
 */
void anvl_add_output(Output* output) {
    output->seltag = 0;
    for (int i = 0; i < LENGTH(tags); i++) {
        Tag* tag = calloc(1, sizeof(Tag));

        tag->n = i;
        tag->sym = tags[i];
        tag->focused = NULL;
        tag->lt = &layouts[0];

        output->tags[i] = tag;
    }

    wl_list_insert(&anvl.outputs, &output->link);
    if (selmon == NULL)
        selmon = output;
}

/*
 * Remove an output and release its tag state.
 *
 * Windows belonging to the removed output are detached from it. A future
 * improvement could move them automatically to another available output.
 */
void anvl_remove_output(Output* output) {
    wl_list_remove(&output->link);

    Window* window;
    wl_list_for_each(window, &anvl.windows, link) {
        if (window->mon == output)
            window->mon = NULL;
    }

    for (int i = 0; i < LENGTH(output->tags); i++)
        free(output->tags[i]);

    if (selmon == output)
        selmon = NULL;

    free(output);
}

/* Update the compositor-space position of an output. */
void anvl_output_position(Output* output, int x, int y) {
    output->x = x;
    output->y = y;
}

/* Update the logical dimensions of an output. */
void anvl_output_dimensions(Output* output, int width, int height) {
    output->width = width;
    output->height = height;
}

/* Apply the current WM state to River.
 *
 * Focus state is updated first, all windows are prepared, and then each
 * output's currently selected layout places its visible windows.
 */
void anvl_manage(void) {
    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) { manage_seat(seat); }

    Window* window;
    wl_list_for_each(window, &anvl.windows, link) {
        river_window_prepare(window);
    }

    Output* output;
    wl_list_for_each(output, &anvl.outputs, link) {
        output->tags[output->seltag]->lt->manage(output);
    }
}
