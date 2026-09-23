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

WindowManager anvl;
Output* selmon = NULL;

void destroy_window(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        river_window_close(seat->focused);
    }
}

void focus_next_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Output* next = wl_container_of(selmon->link.next, selmon, link);
        if (next != NULL && &next->link != &anvl.outputs) {
            selmon = next;
            river_select_output(selmon);
            river_pointer_warp(seat, selmon->x + selmon->width / 2, selmon->y + selmon->height / 2);
        }
    }
}

void focus_prev_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Output* prev = wl_container_of(selmon->link.prev, selmon, link);
        if (prev != NULL && &prev->link != &anvl.outputs) {
            selmon = prev;
            river_select_output(selmon);
            river_pointer_warp(seat, selmon->x + selmon->width / 2, selmon->y + selmon->height / 2);
        }
    }
}

void tag_next_mon(Seat* seat, Arg* arg) {
    if (selmon == NULL || seat->focused == NULL)
        return;

    Output* next = wl_container_of(selmon->link.next, next, link);

    if (&next->link == &anvl.outputs)
        return;

    seat->focused->mon = next;
    seat->focused->tag = next->seltag;

    next->tags[next->seltag]->focused = seat->focused;
}

void tag_prev_mon(Seat* seat, Arg* arg) {
    if (selmon == NULL || seat->focused == NULL)
        return;

    Output* prev = wl_container_of(selmon->link.prev, prev, link);

    if (&prev->link == &anvl.outputs)
        return;

    Window* window = seat->focused;

    window->mon = prev;
    window->tag = prev->seltag;

    prev->tags[prev->seltag]->focused = window;
}

void exit_session(Seat* seat, Arg* arg) {
    river_exit_session();
}

void set_layout(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->tags[selmon->seltag]->lt = arg->v;
    }
}

// TODO: loop and only through windows on selmon
void focus_next(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        Window* next = wl_container_of(seat->focused->link.next, seat->focused, link);
        if (next != NULL && &next->link != &anvl.windows) {
            seat->focused = next;
            river_pointer_warp(seat, seat->focused->x + seat->focused->width / 2, seat->focused->y + seat->focused->height / 2);
        }
    }
}

// TODO: loop and only through windows on selmon
void focus_prev(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        Window* prev = wl_container_of(seat->focused->link.prev, seat->focused, link);
        if (prev != NULL && &prev->link != &anvl.windows) {
            seat->focused = prev;
            river_pointer_warp(seat, seat->focused->x + seat->focused->width / 2, seat->focused->y + seat->focused->height / 2);
        }
    }
}

void view(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->seltag = arg->u;
    }
}

void tag(Seat* seat, Arg* arg) {
    if (seat->focused == NULL || selmon == NULL)
        return;

    Window* window = seat->focused;

    window->mon = selmon;
    window->tag = arg->u;

    selmon->tags[arg->u]->focused = window;
}

void spawn(Seat* seat, Arg* arg) {
    if (fork() == 0)
        execvp(((char**)arg->v)[0], (char**)arg->v);
}

// TODO: reconsider how windows are treated here
// Used for dragging
// Used for dragging

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

void anvl_output_position(Output* output, int x, int y) {
    output->x = x;
    output->y = y;
}

void anvl_output_dimensions(Output* output, int width, int height) {
    output->width = width;
    output->height = height;
}

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

void manage_seat(Seat* seat) {
    if (seat->focused == NULL && !wl_list_empty(&anvl.windows)) {
        seat->focused = wl_container_of(anvl.windows.prev, seat->focused, link);
    }

    if (seat->focused != NULL) {
        river_focus_window(seat, seat->focused);
        river_raise_window(seat->focused);

        seat->focused->mon
            ->tags[seat->focused->tag]
            ->focused = seat->focused;
    } else {
        river_clear_focus(seat);
    }
}

void tile(Output* output) {
    uint32_t n = 0;

    Window* window;
    wl_list_for_each(window, &anvl.windows, link) {
        if (window->mon == output && window->tag == output->seltag)
            n++;
    }

    if (n == 0)
        return;

    int x = output->x + gappx;
    int y = output->y + (show_bar && top_bar ? barpx : 0) + gappx;

    int width = output->width - 2 * gappx;
    int height = output->height
        - (show_bar ? barpx : 0)
        - 2 * gappx;

    int window_width = (width - (n - 1) * gappx) / n;

    uint32_t i = 0;

    wl_list_for_each(window, &anvl.windows, link) {
        if (window->mon != output || window->tag != output->seltag)
            continue;

        int wx = x + i * (window_width + gappx);

        /*
         * Give rounding remainder to final window.
         */
        int ww = i == n - 1
            ? x + width - wx
            : window_width;

        river_window_show(window);
        river_window_move(window, wx, y);
        river_window_resize(window, ww, height);

        i++;
    }
}

void monocle(Output* output) {
    Window* window;

    wl_list_for_each(window, &anvl.windows, link) {
        if (window->mon != output || window->tag != output->seltag)
            continue;

        river_window_show(window);

        river_window_move(
            window,
            output->x,
            output->y + (show_bar && top_bar ? barpx : 0));

        river_window_resize(
            window,
            output->width,
            output->height - (show_bar ? barpx : 0));
    }
}

// https://wayland-book.com/surfaces/shared-memory.html
// ---
// ---

/* Keep these in sync with dwm/palette.h. */

// credit to
// https://git.sr.ht/~zuki/zrwm/tree/afc021dd91bba7a69b1f10fbbf8c5d7bfd66490a/item/zrwm.c#L636
int main() {
    if (!river_init())
        return 1;

    struct wl_display* display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to connect to Wayland server\n");
        return 1;
    }

    signal(SIGCHLD, SIG_IGN);

    wl_list_init(&anvl.wl_outputs);
    wl_list_init(&anvl.keyboards);
    wl_list_init(&anvl.windows);
    wl_list_init(&anvl.outputs);
    wl_list_init(&anvl.seats);

    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_DEBUG);
    if (!set_font_scale(1)) {
        return 1;
    }

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "roundtrip failed\n");
        return 1;
    }

    if (!river_supported()) {
        fprintf(stderr, "river_window_manager_v1 or river_xkb_bindings_v1 not "
                        "supported by the Wayland server\n");
        return 1;
    }

    while (true) {
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "dispatch failed\n");
            return 1;
        }
    }

    fcft_destroy(fcft_font);

    return 0;
}
