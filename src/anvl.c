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
#include "config.h"

#define MIN(A, B) (A < B ? A : B)
#define MAX(A, B) (A > B ? A : B)
#define LENGTH(A) (sizeof A / sizeof A[0])
#define ISVISIBLE(C) (C->tagmask & C->mon->tagmask)
#define CLAMP(VAL, MIN, MAX) VAL = VAL < MIN ? MIN : (VAL > MAX ? MAX : VAL)

WindowManager anvl;
Output* selmon = NULL;

struct wl_shm* shm;
struct wl_compositor* compositor;
struct zwlr_layer_shell_v1* zwlr_layer_shell;

struct xkb_context* xkb_context;
struct river_xkb_config_v1* xkb_config;
struct river_xkb_keymap_v1* xkb_keymap;
struct river_layer_shell_v1* layer_shell;
struct river_xkb_bindings_v1* xkb_bindings;
struct river_input_manager_v1* input_manager;
struct river_window_manager_v1* window_manager;

void destroy_window(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        river_window_v1_close(seat->focused->river_window);
    }
}

void focus_next_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Output* next = wl_container_of(selmon->link.next, selmon, link);
        if (next != NULL && &next->link != &anvl.outputs) {
            selmon = next;
            river_layer_shell_output_v1_set_default(selmon->river_layer_shell);
            river_seat_v1_pointer_warp(seat->river_seat, selmon->x + selmon->width / 2, selmon->y + selmon->height / 2);
        }
    }
}

void focus_prev_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        Output* prev = wl_container_of(selmon->link.prev, selmon, link);
        if (prev != NULL && &prev->link != &anvl.outputs) {
            selmon = prev;
            river_layer_shell_output_v1_set_default(selmon->river_layer_shell);
            river_seat_v1_pointer_warp(seat->river_seat, selmon->x + selmon->width / 2, selmon->y + selmon->height / 2);
        }
    }
}

void tag_next_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL && seat->focused != NULL) {
        Output* next = wl_container_of(selmon->link.next, selmon, link);
        if (next != NULL && &next->link != &anvl.outputs) {
            remove_node(seat->focused->node);
            insert_node(seat->focused, next->tags[next->seltag]->root, next->tags[next->seltag]->focused);
        }
    }
}

void tag_prev_mon(Seat* seat, Arg* arg) {
    if (selmon != NULL && seat->focused != NULL) {
        Output* prev = wl_container_of(selmon->link.prev, selmon, link);
        if (prev != NULL && &prev->link != &anvl.outputs) {
            remove_node(seat->focused->node);
            insert_node(seat->focused, prev->tags[prev->seltag]->root, prev->tags[prev->seltag]->focused);
        }
    }
}

void exit_session(Seat* seat, Arg* arg) {
    river_window_manager_v1_exit_session(window_manager);
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
            river_seat_v1_pointer_warp(seat->river_seat, seat->focused->x + seat->focused->width / 2, seat->focused->y + seat->focused->height / 2);
        }
    }
}

// TODO: loop and only through windows on selmon
void focus_prev(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        Window* prev = wl_container_of(seat->focused->link.prev, seat->focused, link);
        if (prev != NULL && &prev->link != &anvl.windows) {
            seat->focused = prev;
            river_seat_v1_pointer_warp(seat->river_seat, seat->focused->x + seat->focused->width / 2, seat->focused->y + seat->focused->height / 2);
        }
    }
}

void view(Seat* seat, Arg* arg) {
    if (selmon != NULL) {
        selmon->seltag = arg->u;
    }
}

void tag(Seat* seat, Arg* arg) {
    if (seat->focused != NULL) {
        remove_node(seat->focused->node);
        insert_node(seat->focused, selmon->tags[arg->u]->root, selmon->tags[arg->u]->focused);
    }
}

void spawn(Seat* seat, Arg* arg) {
    if (fork() == 0)
        execvp(((char**)arg->v)[0], (char**)arg->v);
}

Node* create_node(Tag* tag, Window* window, Node* parent) {
    Node* node = calloc(1, sizeof(Node));
    node->tag = tag;
    node->split_type = UNSET;
    node->split_ratio = 0.5;
    node->window = window;
    node->first = node->second = NULL;
    node->parent = parent;

    return node;
}

// Insert window after ref, which has to be a leaf node with a window attached,
// by splitting it If ref is NULL, the tree is assumed to be empty and window is
// inserted on root
void insert_node(Window* window, Node* root, Node* ref) {
    if (ref == NULL) {
        root->window = window;
        window->node = root;
        root->tag->focused = root;
    } else {
        Window* ref_window = ref->window;
        ref->window = NULL;

        if (ref->split_type == UNSET) {
            if (ref->width >= ref->height)
                ref->split_type = VERTICAL;
            else
                ref->split_type = HORIZONTAL;
        }

        Node* first = create_node(root->tag, ref_window, ref);
        ref_window->node = first;

        Node* second = create_node(root->tag, window, ref);
        window->node = second;

        ref->first = first;
        ref->second = second;
        root->tag->focused = second;
    }
}

void remove_node(Node* node) {
    Node* parent = node->parent;
    if (parent == NULL) {
        node->tag->focused = NULL;
        node->split_type = UNSET;
        node->split_ratio = 0.5;
        node->window = NULL;
    } else {
        Node* sibling = parent->first != node ? parent->first : parent->second;

        parent->split_type = sibling->split_type;
        parent->split_ratio = sibling->split_ratio;
        parent->first = sibling->first;
        if (parent->first != NULL)
            parent->first->parent = parent;
        parent->second = sibling->second;
        if (parent->second != NULL)
            parent->second->parent = parent;
        parent->window = sibling->window;
        if (parent->window != NULL)
            parent->window->node = parent;

        free(node);
        free(sibling);
        // TODO: at this point focused is now invalid, it does not seem as if this
        // is a problem
        //  as it is reassigned on the following manage sequence, however this
        //  should still be fixed.
    }
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

void manage_seat(Seat* seat) {
    if (seat->focused == NULL && !wl_list_empty(&anvl.windows)) {
        seat->focused = wl_container_of(anvl.windows.prev, seat->focused, link);
    }

    if (seat->focused != NULL) {
        river_seat_v1_focus_window(seat->river_seat, seat->focused->river_window);
        river_node_v1_place_top(seat->focused->river_node);
        seat->focused->node->tag->focused = seat->focused->node;
    } else {
        river_seat_v1_clear_focus(seat->river_seat);
    }
}

void tile(Output* output) {
    Node* queue[1 << 16];
    uint32_t front = 0;
    uint32_t back = 0;

    queue[back++] = output->tags[output->seltag]->root;

    Node* n;
    while (front != back) {
        n = queue[front++];
        if (n->first != NULL && n->second != NULL) {
            queue[back++] = n->first;
            queue[back++] = n->second;
        } else if (n->window != NULL) {
            river_window_v1_show(n->window->river_window);
            window_set_dimensions(n->window, n->window->node->width, n->window->node->height);
            window_set_position(n->window, n->window->node->x, n->window->node->y);
        }
    }
}

void monocle(Output* output) {
    Node* queue[1 << 16];
    uint32_t front = 0;
    uint32_t back = 0;

    queue[back++] = output->tags[output->seltag]->root;

    Node* n;
    while (front != back) {
        n = queue[front++];
        if (n->first != NULL && n->second != NULL) {
            queue[back++] = n->first;
            queue[back++] = n->second;
        } else if (n->window != NULL) {
            river_window_v1_show(n->window->river_window);
            window_set_position(n->window, output->x, output->y + (show_bar && top_bar ? barpx : 0));
            window_set_dimensions(n->window, output->width, output->height - (show_bar ? barpx : 0));
        }
    }
}

// https://wayland-book.com/surfaces/shared-memory.html
// ---
// ---

/* Keep these in sync with dwm/palette.h. */

// credit to
// https://git.sr.ht/~zuki/zrwm/tree/afc021dd91bba7a69b1f10fbbf8c5d7bfd66490a/item/zrwm.c#L636
int main() {
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

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

    if (window_manager == NULL || xkb_bindings == NULL) {
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
