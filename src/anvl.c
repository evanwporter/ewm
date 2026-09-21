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
#include <string.h>
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
#define ISVISIBLE(C) ((C)->mon == selmon && (C)->workspace == selmon->selected_workspace)
#define CLAMP(VAL, MIN, MAX) VAL = VAL < MIN ? MIN : (VAL > MAX ? MAX : VAL)

WindowManager anvl;
Output* selmon = NULL;

static void detachclient(Output* output, Client* client);
static void detachstack(Output* output, Client* client);
static void attachclient(Output* output, Client* client);
static void attachstack(Output* output, Client* client);
static Client* firstvisible(Output* output);
static Client* focused_or_firstvisible(Output* output);
static void swallow(Client* client);
static void update_selected_border(Output* output);

void destroy_window(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        Client* client = seat->focused->swallowing ? seat->focused->swallowing : seat->focused;
        river_window_close(client);
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
            client->workspace = next->selected_workspace;
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
            client->workspace = prev->selected_workspace;
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
        SELECTED_WORKSPACE(selmon)->lt = arg->v;
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
        Workspace* workspace = SELECTED_WORKSPACE(selmon);
        workspace->master_ratio = MAX(0.1, MIN(0.9, workspace->master_ratio + arg->f));
    }
}

/// User function to increment or decrement the number of client windows in the master area.
void incnmaster(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Workspace* workspace = SELECTED_WORKSPACE(selmon);

        /* This adjusts the number of master clients with the given argument. The
         * MAX(..., 0) is a safeguard to prevent the master count becoming negative. */
        workspace->master_count = MAX((int)workspace->master_count + arg->i, 0);
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
        if (client->workspace == output->selected_workspace)
            return client;
    return NULL;
}

/* Restore the workspace's last focused client when returning to it. */
static Client* focused_or_firstvisible(Output* output) {
    Workspace* workspace = SELECTED_WORKSPACE(output);
    Client* client = workspace->selected;

    if (client != NULL && client->mon == output && client->workspace == output->selected_workspace)
        return client;
    return firstvisible(output);
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
        SELECTED_WORKSPACE(selmon)->selected = client;
        Client* focused = client->swallowing ? client->swallowing : client;
        river_focus_window(seat, focused);
        river_raise_window(focused);
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
    if (selmon == NULL || arg->u > LENGTH(workspace_names) || (arg->u != 0 && arg->u == selmon->selected_workspace))
        return;

    if (arg->u == 0) {
        unsigned int workspace = selmon->selected_workspace;
        selmon->selected_workspace = selmon->previous_workspace;
        selmon->previous_workspace = workspace;
    } else {
        selmon->previous_workspace = selmon->selected_workspace;
        selmon->selected_workspace = arg->u;
    }
    focus_client(seat, focused_or_firstvisible(selmon));
}

void sendtoworkspace(Seat* seat, Arg* arg) {
    if (selmon == NULL || selmon->sel == NULL || arg->u == 0 || arg->u > LENGTH(workspace_names) || selmon->sel->workspace == arg->u)
        return;

    Client* client = selmon->sel;
    Workspace* workspace = SELECTED_WORKSPACE(selmon);

    if (workspace->selected == client)
        workspace->selected = NULL;
    client->workspace = arg->u;
    focus_client(seat, focused_or_firstvisible(selmon));
}

void movetoworkspace(Seat* seat, Arg* arg) {
    if (selmon == NULL || selmon->sel == NULL)
        return;

    sendtoworkspace(seat, arg);
    viewworkspace(seat, arg);
}

void spawn(Seat* seat, Arg* arg) {
    if (fork() == 0)
        execvp(((char**)arg->v)[0], (char**)arg->v);
}

/*
 * Apply the available Wayland window metadata. app_id is the closest analogue
 * to an X11 class; there is no Wayland instance field. Keep scanning after a
 * match so configuration can provide broad defaults followed by exceptions.
 */
static void applyrules(Client* client) {
    client->isterminal = 0;

    for (size_t i = 0; i < LENGTH(rules); i++) {
        const Rule* rule = &rules[i];

        if ((!rule->app_id || (client->app_id && strstr(client->app_id, rule->app_id))) && (!rule->title || (client->title && strstr(client->title, rule->title))))
            client->isterminal = rule->isterminal;
    }
}

static pid_t parent_pid(pid_t pid) {
#ifdef __linux__
    char path[64];
    char state;
    pid_t parent = 0;
    FILE* file;

    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    file = fopen(path, "r");
    if (file != NULL) {
        if (fscanf(file, "%*d %*s %c %d", &state, &parent) != 2)
            parent = 0;
        fclose(file);
    }
    return parent;
#else
    return 0;
#endif
}

static bool is_descendant(pid_t ancestor, pid_t client) {
    while (client != 0 && client != ancestor)
        client = parent_pid(client);
    return client == ancestor;
}

static Client* terminal_for(const Client* client) {
    Output* output;
    wl_list_for_each(output, &anvl.outputs, link) for (Client* candidate = output->clients; candidate != NULL; candidate = candidate->next) if (candidate->isterminal && candidate->swallowing == NULL && candidate->pid != 0 && is_descendant(candidate->pid, client->pid)) return candidate;
    return NULL;
}

static void unswallow(Client* terminal) {
    Client* child = terminal->swallowing;
    if (child == NULL)
        return;

    terminal->swallowing = NULL;
    child->swallowed_by = NULL;
    river_window_show(terminal);
}

static void swallow(Client* client) {
    Client* terminal;

    if (client->pid == 0 || client->isterminal || client->noswallow || (!swallow_floating && client->isfloating) || client->swallowed_by != NULL)
        return;
    terminal = terminal_for(client);
    if (terminal == NULL)
        return;

    detachclient(client->mon, client);
    detachstack(client->mon, client);
    client->mon = terminal->mon;
    client->workspace = terminal->workspace;
    client->swallowed_by = terminal;
    terminal->swallowing = client;
    if (terminal->mon->sel == client)
        terminal->mon->sel = terminal;
    if (SELECTED_WORKSPACE(terminal->mon)->selected == client)
        SELECTED_WORKSPACE(terminal->mon)->selected = terminal;
    river_window_hide(terminal);
}

void anvl_set_client_app_id(Client* client, const char* app_id) {
    free(client->app_id);
    client->app_id = app_id == NULL ? NULL : strdup(app_id);
    applyrules(client);
    swallow(client);
}

void anvl_set_client_title(Client* client, const char* title) {
    free(client->title);
    client->title = title == NULL ? NULL : strdup(title);
    applyrules(client);
    swallow(client);
}

void anvl_set_client_pid(Client* client, pid_t pid) {
    client->pid = pid > 0 ? pid : 0;
    swallow(client);
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
    window->workspace = selmon->selected_workspace;
    attachclient(selmon, window);
    attachstack(selmon, window);
    wl_list_insert(&anvl.windows, &window->link);

    Seat* seat;
    wl_list_for_each(seat, &anvl.seats, link) { focus_client(seat, window); }
}

void anvl_remove_window(Client* window) {
    Output* output = window->mon;

    if (window->swallowed_by != NULL)
        unswallow(window->swallowed_by);
    if (window->swallowing != NULL)
        window->swallowing->swallowed_by = NULL;

    Output* focused_output;
    wl_list_for_each(focused_output, &anvl.outputs, link) {
        for (size_t i = 0; i < LENGTH(focused_output->workspaces); i++) {
            if (focused_output->workspaces[i]->selected == window)
                focused_output->workspaces[i]->selected = NULL;
        }
    }

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
    output->selected_workspace = 1;
    output->previous_workspace = 1;
    for (int i = 0; i < LENGTH(workspace_names); i++) {
        Workspace* workspace = calloc(1, sizeof(Workspace));
        workspace->n = i;
        workspace->sym = workspace_names[i];
        workspace->lt = &layouts[0];
        workspace->master_ratio = default_master_ratio;
        workspace->master_count = default_master_count;
        output->workspaces[i] = workspace;
    }

    wl_list_insert(&anvl.outputs, &output->link);
    if (selmon == NULL)
        selmon = output;
}

void anvl_remove_output(Output* output) {
    wl_list_remove(&output->link);
    for (int i = 0; i < LENGTH(output->workspaces); i++) {
        free(output->workspaces[i]);
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
        river_window_set_selected_border(window, false);
    }

    Output* output;
    wl_list_for_each(output, &anvl.outputs, link) {
        SELECTED_WORKSPACE(output)->lt->manage(output);
        for (Client* client = output->clients; client != NULL; client = client->next) {
            Client* child = client->swallowing;
            if (child == NULL)
                continue;
            river_window_hide(client);
            river_window_show(child);
            river_window_move(child, client->x, client->y);
            river_window_resize(child, client->width, client->height);
        }
        update_selected_border(output);
    }
}

/* Only distinguish focus when tiling more than one client. Monocle intentionally has no border. */
static void update_selected_border(Output* output) {
    unsigned int visible = 0;

    for (Client* client = output->clients; client != NULL; client = client->next)
        if (client->workspace == output->selected_workspace)
            visible++;

    if (visible < 2 || SELECTED_WORKSPACE(output)->lt->manage == monocle)
        return;

    if (output->sel != NULL) {
        Client* focused = output->sel->swallowing ? output->sel->swallowing : output->sel;
        river_window_set_selected_border(focused, true);
    }
}

void manage_seat(Seat* seat) {
    if (selmon != NULL)
        focus_client(seat, selmon->sel);
}

void monocle(Output* output) {
    for (Client* client = output->clients; client != NULL; client = client->next) {
        if (client->workspace != output->selected_workspace)
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
