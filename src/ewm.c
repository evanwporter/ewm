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

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fcft/fcft.h>

#include "bar.h"
#include "config.h"
#include "ewm.h"
#include "river.h"

#define MIN(A, B) (A < B ? A : B)
#define MAX(A, B) (A > B ? A : B)
#define LENGTH(A) (sizeof A / sizeof A[0])

/// Checks to whether the Client is displayed
/// on the Client's Monitors current tags
#define ISVISIBLE(C) (C->tag & C->mon->tagmask)

#define CLAMP(VAL, MIN, MAX) VAL = VAL < MIN ? MIN : (VAL > MAX ? MAX : VAL)

WindowManager ewm;
Output* selmon = NULL;

/* Close the currently focused window, if one exists. */
void destroy_window(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        river_window_close(seat->focused);
    }
}

/* Request that River terminate the current compositor session. */
void exit_session(Seat* seat, Arg* arg) {
    river_exit_session();
}

/* Set the layout used by the currently selected tag. */
void set_layout(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->tags[selmon->seltag]->lt = arg->v;
    }
}

static bool focusable_on_selected_tag(const Client* client) {
    return selmon != NULL && client->mon == selmon && client->tag == selmon->seltag
        && !client->hidden;
}

/* Find the next/previous visible client on the selected output, wrapping at
 * either end */
static Client* focusstack_client(Client* current, bool forward) {
    if (selmon == NULL || wl_list_empty(&ewm.windows))
        return NULL;

    struct wl_list* node = current == NULL
        ? (forward ? ewm.windows.next : ewm.windows.prev)
        : (forward ? current->link.next : current->link.prev);

    while (node != &ewm.windows) {
        Client* client = wl_container_of(node, client, link);
        if (focusable_on_selected_tag(client))
            return client;
        node = forward ? node->next : node->prev;
    }

    node = forward ? ewm.windows.next : ewm.windows.prev;
    while (node != &ewm.windows) {
        Client* client = wl_container_of(node, client, link);
        if (focusable_on_selected_tag(client))
            return client;
        node = forward ? node->next : node->prev;
    }

    return NULL;
}

/* Select the next visible window in client/layout order. */
void focus_next(Seat* seat, Arg* arg) {
    Client* next = focusstack_client(seat->focused, true);
    if (next != NULL) {
        anvl_focus_client(seat, next);
        river_pointer_warp(seat, next->x + next->width / 2, next->y + next->height / 2);
    }
}

/* Select the previous visible window in client/layout order. */
void focus_prev(Seat* seat, Arg* arg) {
    Client* prev = focusstack_client(seat->focused, false);
    if (prev != NULL) {
        anvl_focus_client(seat, prev);
        river_pointer_warp(seat, prev->x + prev->width / 2, prev->y + prev->height / 2);
    }
}

/* Switch the selected output to the requested tag. */
void view(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->seltag = arg->u;
    }
}

/* Move the focused window to a different tag on the selected output. */
void tag(Seat* seat, Arg* arg) {
    if (seat->focused == NULL || selmon == NULL || arg->u >= LENGTH(selmon->tags))
        return;

    Client* window = seat->focused;
    Output* old_output = window->mon;

    if (old_output == selmon && window->tag == arg->u)
        return;

    /* The source tag must not retain a pointer to a client that no longer
     * belongs to it.  In particular, that stale pointer would be considered
     * when the source tag is viewed again. */
    if (old_output != NULL && old_output->tags[window->tag]->focused == window)
        old_output->tags[window->tag]->focused = NULL;

    wl_list_remove(&window->focus_link);
    window->mon = selmon;
    window->tag = arg->u;
    wl_list_insert(&selmon->focus_stack, &window->focus_link);

    /* Remember the client for its destination tag, but do not make a client
     * on an unselected tag the seat's keyboard focus.  manage_seat() will
     * select the next visible client on the current tag. */
    selmon->tags[window->tag]->focused = window;
    if (window->tag == selmon->seltag)
        anvl_focus_client(seat, window);
    else
        seat->focused = NULL;
}

/* Fork and execute the command stored in arg->v. */
void spawn(Seat* seat, Arg* arg) {
    if (fork() == 0)
        execvp(((char**)arg->v)[0], (char**)arg->v);
}

/* User function to move the selected client to become the new master client. If the selected
 * client is the master client then the master and the next tiled window will swap places. */
void zoom(Seat* seat, Arg* arg) {
    if (seat->focused == NULL)
        return;

    Client* client = seat->focused;

    if (client->is_floating || !ISVISIBLE(client))
        return;

    wl_list_remove(&client->link);
    wl_list_insert(&ewm.windows, &client->link);
}

/*
 * Synchronize a seat's logical focus with River.
 *
 * If the seat has no focused window, a fallback window is selected. The
 * focused window is raised and recorded as the focused window for its tag.
 */
void manage_seat(Seat* seat) {
    if (!focusable_on_selected_tag(seat->focused)) {
        Client* saved = selmon == NULL ? NULL : selmon->tags[selmon->seltag]->focused;
        seat->focused = focusable_on_selected_tag(saved)
            ? saved
            : focusstack_client(NULL, true);
    }

    if (seat->focused != NULL) {
        anvl_focus_client(seat, seat->focused);
        river_focus_window(seat, seat->focused);
        river_raise_window(seat->focused);
    } else {
        river_clear_focus(seat);
    }
}

/* Arrange all visible windows in equal-width columns.
 *
 * Only windows belonging to this output and its currently selected tag are
 * considered. Geometry is computed fresh on every layout pass; no persistent
 * layout tree or per-window split state is stored.
 */
void tile(Output* output) {
    Client* client;
    uint32_t n = 0;

    /*
     * Count tiled clients.
     */
    wl_list_for_each(client, &ewm.windows, link) {
        if (client->mon != output
            || client->tag != output->seltag
            || client->is_floating
            || client->hidden) {
            continue;
        }

        n++;
    }

    if (n == 0)
        return;

    Tag* tag = output->tags[output->seltag];

    unsigned int nmaster = tag->nmaster;
    float mfact = tag->mfact;

    /*
     * Usable output geometry.
     */
    int x = output->x + gappx;
    int y = output->y
        + (show_bar && top_bar ? barpx : 0)
        + gappx;

    int width = output->width - 2 * gappx;
    int height = output->height
        - (show_bar ? barpx : 0)
        - 2 * gappx;

    /*
     * Determine width of master area.
     *
     * If everything fits in the master area, it gets the full width.
     */
    int master_width;

    if (n > nmaster) {
        if (nmaster > 0)
            master_width = (int)((width - gappx) * mfact);
        else
            master_width = 0;
    } else {
        master_width = width;
    }

    unsigned int i = 0;
    unsigned int master_i = 0;
    unsigned int stack_i = 0;

    wl_list_for_each(client, &ewm.windows, link) {
        if (client->mon != output
            || client->tag != output->seltag
            || client->is_floating
            || client->hidden) {
            continue;
        }

        int cx;
        int cy;
        int cw;
        int ch;

        /*
         * Master side.
         */
        if (i < nmaster) {
            unsigned int count = MIN(n, nmaster);

            int usable_height = height - (count - 1) * gappx;

            int base_height = usable_height / count;

            cx = x;
            cy = y + master_i * (base_height + gappx);

            cw = master_width;

            /*
             * Give any division remainder to the final client.
             */
            if (master_i == count - 1)
                ch = y + height - cy;
            else
                ch = base_height;

            master_i++;
        }

        /*
         * Stack side.
         */
        else {
            unsigned int count = n - nmaster;

            int usable_height = height - (count - 1) * gappx;

            int base_height = usable_height / count;

            cx = x + master_width + gappx;
            cy = y + stack_i * (base_height + gappx);

            cw = width - master_width - gappx;

            if (stack_i == count - 1)
                ch = y + height - cy;
            else
                ch = base_height;

            stack_i++;
        }

        river_window_show(client);
        river_window_move(client, cx, cy);
        river_window_resize(client, cw, ch);

        i++;
    }
}

void hide_window(Client* client) {
    if (client->hidden)
        return;

    client->hidden = true;
    river_window_hide(client);
}

void show_client(Client* client) {
    if (!client->hidden)
        return;

    client->hidden = false;
    river_window_show(client);
}

/* Stack all visible windows on top of one another at full output size.
 *
 * Only the currently focused/raised window is normally visible to the user,
 * but every window on the selected tag receives identical geometry.
 */
void monocle(Output* output) {
    Client* window;

    wl_list_for_each(window, &ewm.windows, link) {
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

    wl_list_init(&ewm.wl_outputs);
    wl_list_init(&ewm.keyboards);
    wl_list_init(&ewm.windows);
    wl_list_init(&ewm.outputs);
    wl_list_init(&ewm.seats);

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
