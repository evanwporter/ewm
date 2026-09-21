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
#define ISVISIBLE(C) ((C)->mon == selmon && (C)->workspace == selmon->selected_workspaces[selmon->sel_ws])
#define CLAMP(VAL, MIN, MAX) VAL = VAL < MIN ? MIN : (VAL > MAX ? MAX : VAL)

WindowManager anvl;
Output* selmon = NULL;

static void detachclient(Output* output, Client* client);
static void detachstack(Output* output, Client* client);
static void attachclient(Output* output, Client* client);
static void attachstack(Output* output, Client* client);
static Client* firstvisible(Output* output);

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
            focus_client(seat, selmon->sel);
            river_pointer_warp(seat, selmon->mx + selmon->width / 2, selmon->my + selmon->height / 2);
        }
    }
}

void focus_prev_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Output* prev = wl_container_of(selmon->link.prev, selmon, link);
        if (prev != NULL && &prev->link != &anvl.outputs) {
            selmon = prev;
            river_select_output(selmon);
            focus_client(seat, selmon->sel);
            river_pointer_warp(seat, selmon->mx + selmon->width / 2, selmon->my + selmon->height / 2);
        }
    }
}

void tag_next_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL && seat->focused != NULL) {
        Output* next = wl_container_of(selmon->link.next, selmon, link);
        if (next != NULL && &next->link != &anvl.outputs) {
            Client* client = seat->focused;
            detachclient(selmon, client);
            detachstack(selmon, client);
            client->mon = next;
            client->workspace = next->selected_workspaces[next->sel_ws];
            attachclient(next, client);
            attachstack(next, client);
            focus_client(seat, client);
        }
    }
}

void tag_prev_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL && seat->focused != NULL) {
        Output* prev = wl_container_of(selmon->link.prev, selmon, link);
        if (prev != NULL && &prev->link != &anvl.outputs) {
            Client* client = seat->focused;
            detachclient(selmon, client);
            detachstack(selmon, client);
            client->mon = prev;
            client->workspace = prev->selected_workspaces[prev->sel_ws];
            attachclient(prev, client);
            attachstack(prev, client);
            focus_client(seat, client);
        }
    }
}

void exit_session(Seat* seat, Arg* arg) {
    river_exit_session();
}

void setlayout(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->tags[selmon->seltag]->lt = arg->v;
    }
}

/* User function to set or adjust the master / stack factor (or ratio if you wish) for the
 * selected monitor.
 *
 * The mfact is a floating value with:
 *    - a minimum value of 0.05 (5% of the window area) and
 *    - a maximum value of 0.95 (95% of the window area)
 *
 * As per the default configuration this factor is adjusted with increments or decrements of 0.05
 * using the MOD+l and MOD+h keybindings.
 *
 * The default mfact value is 0.55 giving the master area slightly more space than the stack area.
 *
 * Optionally the user can pass a value greater than 1.0 to set an absolute value, in which case
 * 1.0 will be subtracted from the given value. For example the following keybinding would
 * explicitly set the mfact value to 0.5:
 *
 *     { MODKEY,                       XK_u,      setmfact,       {.f = 1.50} },
 *
 * When setting the mfact value absolutely the value given (less the subtracted 1.0) must fall
 * within the minimum and maximum boundaries for the master / stack factor - otherwise the value
 * will simply be ignored.
 */
/* arg > 1.0 will set mfact absolutely */
void setmfact(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Workspace* tag = selmon->tags[selmon->seltag];
        tag->master_ratio = MAX(0.1, MIN(0.9, tag->master_ratio + arg->f));
    }
}

/// User function to increment or decrement the number of client windows in the master area.
void incnmaster(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Workspace* tag = selmon->tags[selmon->seltag];

        /* This adjusts the number of master clients with the given argument. The
         * MAX(..., 0) is a safeguard to prevent the master count becoming negative. */
        tag->master_count = MAX((int)tag->master_count + arg->i, 0);
    }
}

static void detachstack(Output* output, Client* client) {
    Client** current;

    for (current = &output->stack; *current && *current != client; current = &(*current)->snext)
        ;
    if (*current != NULL)
        *current = client->snext;
}

static void detachclient(Output* output, Client* client) {
    Client** current;

    for (current = &output->clients; *current && *current != client; current = &(*current)->next)
        ;
    if (*current != NULL)
        *current = client->next;
}

static void attachstack(Output* output, Client* client) {
    client->snext = output->stack;
    output->stack = client;
}

static void attachclient(Output* output, Client* client) {
    client->next = output->clients;
    output->clients = client;
}

static Client* firstvisible(Output* output) {
    for (Client* client = output->stack; client != NULL; client = client->snext)
        if (client->workspace == output->selected_workspaces[output->sel_ws])
            return client;
    return NULL;
}

void focus_client(Seat* seat, Client* client) {
    if (selmon == NULL)
        return;

    if (client != NULL && client->mon != selmon)
        selmon = client->mon;
    if (client != NULL && client != selmon->sel) {
        detachstack(selmon, client);
        attachstack(selmon, client);
    }

    selmon->sel = client;
    seat->focused = client;
    if (client != NULL) {
        river_focus_window(seat, client);
        river_raise_window(client);
    } else {
        river_clear_focus(seat);
    }
}

/// User function to move the focused client forward or backward through the client list.
void focusstack(Seat* seat, Arg* arg) {
    Client* client = NULL;
    Client* iterator = NULL;
    int inc = arg->i;

    if (selmon == NULL || (!selmon->sel) || (selmon->sel->isfullscreen && lock_fullscreen) || !selmon->clients)
        return;

    if (inc > 0) {
        for (client = selmon->sel->next; client && !ISVISIBLE(client); client = client->next)
            ;
        if (client == NULL)
            for (client = selmon->clients; client && !ISVISIBLE(client); client = client->next)
                ;
    } else {
        for (iterator = selmon->clients; iterator && iterator != selmon->sel; iterator = iterator->next)
            if (ISVISIBLE(iterator))
                client = iterator;
        if (client == NULL)
            for (; iterator; iterator = iterator->next)
                if (ISVISIBLE(iterator))
                    client = iterator;
    }

    if (client != NULL)
        focus_client(seat, client);
}

void focus_next(Seat* seat, Arg* arg) {
    Arg next = { .i = +1 };
    focusstack(seat, &next);
}

void focus_prev(Seat* seat, Arg* arg) {
    Arg prev = { .i = -1 };
    focusstack(seat, &prev);
}

void viewworkspace(Seat* seat, Arg* arg) {
    if (selmon == NULL || arg->u > LENGTH(tags) || (arg->u != 0 && arg->u == selmon->selected_workspaces[selmon->sel_ws]))
        return;

    selmon->sel_ws ^= 1;
    if (arg->u != 0)
        selmon->selected_workspaces[selmon->sel_ws] = arg->u;
    selmon->seltag = selmon->selected_workspaces[selmon->sel_ws] - 1;
    focus_client(seat, firstvisible(selmon));
}

void sendtoworkspace(Seat* seat, Arg* arg) {
    if (selmon == NULL || selmon->sel == NULL || arg->u == 0 || arg->u > LENGTH(tags) || selmon->sel->workspace == arg->u)
        return;

    selmon->sel->workspace = arg->u;
    focus_client(seat, firstvisible(selmon));
}

void movetoworkspace(Seat* seat, Arg* arg) {
    if (selmon == NULL || selmon->sel == NULL)
        return;

    sendtoworkspace(seat, arg);
    viewworkspace(seat, arg);
}

void view(Seat* seat, Arg* arg) {
    viewworkspace(seat, arg);
}

void tag(Seat* seat, Arg* arg) {
    sendtoworkspace(seat, arg);
}

void spawn(Seat* seat, Arg* arg) {
    if (fork() == 0)
        execvp(((char**)arg->v)[0], (char**)arg->v);
}

void propogate_layout(Node* root) {
    Node* queue[1 << 16];
    uint32_t front = 0;
    uint32_t back = 0;

    queue[back++] = root;

    Node* n;
    while (front != back) {
        n = queue[front++];
        if (n->first != NULL && n->second != NULL) {
            queue[back++] = n->first;
            queue[back++] = n->second;
            n->first->x = n->x;
            n->first->y = n->y;
            n->first->width = n->split_type == VERTICAL
                ? n->width * n->split_ratio - (gappx >> 1)
                : n->width;
            n->first->height = n->split_type == HORIZONTAL
                ? n->height * n->split_ratio - (gappx >> 1)
                : n->height;

            n->second->x = n->split_type == VERTICAL ? n->x + n->first->width + gappx : n->x;
            n->second->y = n->split_type == HORIZONTAL ? n->y + n->first->height + gappx : n->y;
            n->second->width = n->split_type == VERTICAL
                ? n->width - n->first->width - gappx
                : n->width;
            n->second->height = n->split_type == HORIZONTAL
                ? n->height - n->first->height - gappx
                : n->height;
        }
    }
}

// TODO: reconsider how windows are treated here
// Used for dragging
// Used for dragging

void anvl_add_window(Client* window) {
    window->mon = selmon;
    window->workspace = selmon->selected_workspaces[selmon->sel_ws];
    attachclient(selmon, window);
    attachstack(selmon, window);
    wl_list_insert(&anvl.windows, &window->link);

    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) { focus_client(seat, window); }
}

void anvl_remove_window(Client* window) {
    Output* output = window->mon;

    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) {
        if (seat->focused == window) {
            if (output == selmon) {
                Arg next = { .i = +1 };
                focusstack(seat, &next);
            }
            if (seat->focused == window)
                focus_client(seat, NULL);
        }
    }

    if (output != NULL) {
        detachclient(output, window);
        detachstack(output, window);
        if (output->sel == window)
            output->sel = NULL;
    }
    wl_list_remove(&window->link);
}

void anvl_add_output(Output* output) {
    output->seltag = 0;
    output->sel_ws = 0;
    output->selected_workspaces[0] = 1;
    output->selected_workspaces[1] = 1;
    for (int i = 0; i < LENGTH(tags); i++) {
        Workspace* tag = calloc(1, sizeof(Workspace));
        tag->n = i;
        tag->sym = tags[i];
        tag->lt = &layouts[0];
        tag->master_ratio = default_master_ratio;
        tag->master_count = default_master_count;
        output->tags[i] = tag;
    }

    wl_list_insert(&anvl.outputs, &output->link);
    if (selmon == NULL)
        selmon = output;
}

void anvl_remove_output(Output* output) {
    wl_list_remove(&output->link);
    for (int i = 0; i < LENGTH(output->tags); i++) {
        Workspace* tag = output->tags[i];
        free(tag);
    }
    if (selmon == output)
        selmon = NULL;
    free(output);
}

void anvl_output_position(Output* output, int x, int y) {
    output->mx = x;
    output->my = y;
    output->wx = x + gappx;
    output->wy = y + (show_bar && top_bar ? barpx : 0) + gappx;
}

void anvl_output_dimensions(Output* output, int width, int height) {
    output->width = width;
    output->height = height;
    output->mw = width;
    output->mh = height;
    output->ww = width - 2 * gappx;
    output->wh = height - (show_bar ? barpx : 0) - 2 * gappx;
}

void anvl_manage(void) {
    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) { manage_seat(seat); }

    Client* window;
    wl_list_for_each(window, &anvl.windows, link) {
        river_window_prepare(window);
    }

    Output* output;
    wl_list_for_each(output, &anvl.outputs, link) {
        output->tags[output->seltag]->lt->manage(output);
    }
}

void manage_seat(Seat* seat) {
    if (selmon != NULL)
        focus_client(seat, selmon->sel);
}

void monocle(Output* output) {
    for (Client* client = output->clients; client != NULL; client = client->next) {
        if (client->workspace != output->selected_workspaces[output->sel_ws])
            continue;
        river_window_show(client);
        river_window_move(client, output->wx, output->wy);
        river_window_resize(client, output->ww, output->wh);
    }
}

// https://wayland-book.com/surfaces/shared-memory.html
// ---
// ---

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
