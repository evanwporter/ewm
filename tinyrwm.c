// SPDX-FileCopyrightText: © 2026 Isaac Freund
// SPDX-License-Identifier: 0BSD

#include <linux/input-event-codes.h>
#include <river-window-management-v1-client-protocol.h>
#include <river-xkb-bindings-v1-client-protocol.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

/* Helper macros */
#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

/* This macro returns true if any of the given window's tags is on any of the tags currently being
 * viewed on the monitor. */
#define ISVISIBLE(W) ((W)->tags & (W)->mon->tagset[(W)->mon->seltags])

typedef struct Window Window;
typedef struct Output Output;

/// Client
/// In the context of dwm the Client represents a window that is managed by the
/// window manager. A set of clients is represented in form of a linked list.
struct Window {
    /* The name holds the window title. */
    char name[256];

    /* The mina and maxa represents the minimum and maximum aspect ratios as per size hints. */
    float mina, maxa;

    /* The client x, y coordinates and size (width, height). */
    int x, y, w, h;

    /* These variables represent the client's previous size and position and they are maintained
     * in the resizeclient function. In practice in a stock dwm they are only used when a
     * fullscreen window exits fullscreen. */
    int oldx, oldy, oldw, oldh;

    /* These variables are all in relation to size hints.
     *    basew - base width
     *    baseh - base height
     *    incw - width increment
     *    inch - height increment
     *    minw - minimum width
     *    minh - minimum height
     *    maxw - maximum width
     *    maxh - maximum height
     *    hintsvalid - flag indicating whether size hints need to be refreshed
     */
    int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;

    /* Border width (bw) and old border width. The old border width is set in the manage
     * function and is used in the unmanage function in the event that the window was not
     * destroyed. The setfullscreen function also relies on this variable. See comment in the
     * unmanage function. */
    int bw, oldbw;

    /* This represents the tags the client is shown on. This is a bitmask where each bit
     * represents whether the client is shown on that tag.
     *
     * As an example consider the hexadecimal value of 0x51 (decimal 81) which has a binary
     * value of:
     *    001010001  - bitmask
     *    987654321  - tags
     *
     * This would mean that the client is shown on tags 1, 5 and 7.
     */
    unsigned int tags;

    /* Various status flags.
     *    isfixed      - means that the client is fixed in size due to minimum and maximum size
     *                   hints being the same value
     *    isfloating   - indicates whether the client is floating or not
     *    isurgent     - indicates whether the client is urgent or not
     *    neverfocus   - some windows do not want the window manager to give them input focus as
     *                   they are mere passive windows that do not handle any input, the
     *                   neverfocus indicates that the client should never receive input focus
     *                   as indicated by the window manager hints for the window
     *                   (see updatewmhints and setfocus functions)
     *    oldstate     - represents the previous state in the event that the client goes into
     *                   fullscreen, the variable is only used to indicate whether the client
     *                   was floating or not
     *    isfullscreen - indicates whether the window is in fullscreen
     */
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;

    /* The next client in the client list, which is a linked list. The client list controls the
     * order in which clients are tiled. */
    Window* next;

    /* The monitor this client belongs to. */
    Output* mon;

    /// The window object - handles dimensions, lifecycle, and resize
    struct river_window_v1* obj;

    /// Scene graph node - handle where its placed and the order its stacked
    struct river_node_v1* node;

    /* The next client in the stacking order list, which is also a linked list. The stacking
     * order indicates which window is on top of others as well as the order in which clients
     * had focus. */
    Window* snext;

    /// Move into status flags
    bool new;
    bool closed;

    struct Seat* pointer_move_requested;
    struct Seat* pointer_resize_requested;
    uint32_t pointer_resize_requested_edges;

    /// TODO: remove
    struct wl_list link; // WindowManager.windows
};

/* The definition of a key, used in the configuration file when setting up key bindings.
 *
 * static Key keys[] = {
 *    // modifier                     key        function        argument
 *    { MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
 *    { MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
 *    ...
 * };
 */
typedef struct
{
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg*);
    const Arg arg;
} Key;

/* The definition of a layout, used in the configuration file when setting up layouts.
 *
 * static const Layout layouts[] = {
 * 	// symbol     arrange function
 * 	{ "[]=",      tile },    // first entry is default
 * 	{ "><>",      NULL },    // no layout function means floating behavior
 * 	{ "[M]",      monocle },
 * };
 */
typedef struct
{
    const char* symbol;
    void (*arrange)(Output*);
} Layout;

/// Monitor
struct Output {
    struct river_output_v1* obj;
    bool removed;

    /* This holds the layout symbol text, typically as defined in the layouts array. This is
     * used when drawing the layout symbol on the bar. The reason why this is defined for the
     * monitor rather than simply using the layout symbol as defined in the layouts array is
     * that some layouts, like the monocle layout for example, may alter the layout symbol
     * depending on how many clients are present. */
    char ltsymbol[16];

    /* The master / stack factor which controls how big a proportion of the window tiling area
     * is reserved for the master area compared to the stack area. The default value is
     * configured in the configuration file and the value is adjusted via the setmfact function.
     * The value has a range from 0.05 to 0.95. */
    float mfact;

    /* This represents the number of clients that are to be tiled in the master area. This has
     * no upper limit but cannot be less than 0. The default value is configured in the
     * configuration file and the value is adjusted via the incnmaster function. */
    int nmaster;

    /* This represents the monitor number, or the monitor index if you wish. */
    int num;

    /* The by variable defines the bar windows position on the y axis and this is set in the
     * updatebarpos function. */
    int by; /* bar geometry */

    /* These variables represents the position and dimensions of the monitor.
     *    mx - monitor position on the x-axis
     *    my - monitor position on the y-axis
     *    mw - the monitor's width
     *    mh - the monitor's height
     */
    int mx, my, mw, mh; /* screen size */

    /* These variables represents the position and dimensions of the window area, as in the part
     * of the monitor where windows are tiled. This is the space of the monitor excluding the
     * bar window. These are set in the updatebarpos function.
     *    wx - window area position on the x-axis
     *    wy - window area position on the y-axis
     *    ww - the window area's width
     *    wh - the window area's height
     */
    int wx, wy, ww, wh; /* window area  */

    /* The seltags variable is either 0 or 1 and represents the currently selected tagset.
     *
     * This allows for a clever mechanism where one can easily flip between the current and
     * previous tagset by simply flipping the value of seltags:
     *
     *    selmon->seltags ^= 1;
     *
     * For this reason when referring to the selected tags for a monitor you will often find
     * these kind of patterns:
     *
     *    m->tagset[m->seltags]
     *    selmon->tagset[selmon->seltags]
     *    c->mon->tagset[c->mon->seltags]
     *
     * In principle this could just have been defined as two variables for the monitor.
     *
     *    m->tags
     *    m->prevtags
     *
     * which would make the above patterns slightly easier to read, i.e.
     *
     *    m->tags
     *    selmon->tags
     *    c->mon->tags
     *
     * The benefit of using this mechanism, however, is that we save on a single line of code
     * in the view function when the argument is 0 and we toggle back to the previous view.
     */
    unsigned int seltags;

    /* The sellt variable is either 0 or 1 and represents the currently selected layout. This
     * follows the same mechanism as seltags above giving patterns such a:
     *
     *    m->lt[m->sellt]
     *    selmon->lt[selmon->sellt]
     *    c->mon->lt[c->mon->sellt]
     */
    unsigned int sellt;

    /* This array holds the previously and currently viewed tags for the monitor, the index of
     * which is indicated by the seltags variable. */
    unsigned int tagset[2];

    /* Internal flag indicating whether the bar is shown or not. */
    int showbar;

    /* Internal flag indicating whether the bar is shown at the top or at the bottom. */
    int topbar;

    /* The client list. This represents the start of a linked list of clients which determines
     * the order in which clients are tiled. */
    struct wl_list clients;

    /* Monitors are also managed as a linked list with the mons variable referring to the first
     * monitor. The next variable on the monitor refers to the next monitor in the list. */
    struct wl_list stack;

    /* Monitors are also managed as a linked list with the mons variable referring to the first
     * monitor. The next variable on the monitor refers to the next monitor in the list. */
    Output* next;

    // TODO: Remove and use next instead
    struct wl_list link; // WindowManager.outputs

    /* This is the bar window which is used to draw the bar. Each monitor has their own bar. */
    // TODO
    // Window barwin;

    /* This array holds the previous and current layout for the monitor, the index of which is
     * indicated by the sellt variable. */
    const Layout* lt[2];
};

enum Action {
    ACTION_NONE,
    ACTION_SPAWN_FOOT,
    ACTION_CLOSE,
    ACTION_FOCUS_NEXT,
    ACTION_MOVE,
    ACTION_RESIZE,
    ACTION_EXIT,
};

struct XkbBinding {
    struct river_xkb_binding_v1* obj;
    struct Seat* seat;
    enum Action action;
    struct wl_list link;
};

struct PointerBinding {
    struct river_pointer_binding_v1* obj;
    struct Seat* seat;
    enum Action action;
    struct wl_list link;
};

enum SeatOp {
    SEAT_OP_NONE,
    SEAT_OP_MOVE,
    SEAT_OP_RESIZE,
};

/// Collection of inputs (ie: a keyboard and mouse)
struct Seat {
    struct river_seat_v1* obj;
    bool new;
    bool removed;

    struct Window* focused;
    struct Window* hovered;
    struct Window* interacted;

    struct wl_list xkb_bindings; // XkbBinding
    struct wl_list pointer_bindings; // PointerBinding
    enum Action pending_action;

    enum SeatOp op;
    // For SEAT_OP_MOVE and SEAT_OP_RESIZE
    struct Window* op_window;
    int32_t op_start_x, op_start_y;
    int32_t op_dx, op_dy;
    bool op_release;
    // For SEAT_OP_RESIZE only
    int32_t op_start_width, op_start_height;
    uint32_t op_edges;

    struct wl_list link; // WindowManager.seats
};

struct WindowManager {
    struct wl_list outputs; // Output
    struct wl_list windows; // Window
    struct wl_list seats; // Seat
};

struct WindowManager wm;

struct river_window_manager_v1* window_manager_v1;
struct river_xkb_bindings_v1* xkb_bindings_v1;

static void output_handle_removed(void* data, struct river_output_v1* obj) {
    struct Output* output = data;
    output->removed = true;
}

// Ignored events
static void output_handle_wl_output(void* data, struct river_output_v1* obj, uint32_t name) {
}
static void output_handle_position(void* data, struct river_output_v1* obj, int32_t x, int32_t y) {
}
static void output_handle_dimensions(void* data, struct river_output_v1* obj, int32_t width, int32_t height) {
}

const struct river_output_v1_listener river_output_listener = {
    .removed = output_handle_removed,
    .wl_output = output_handle_wl_output,
    .position = output_handle_position,
    .dimensions = output_handle_dimensions,
};

static void output_maybe_destroy(struct Output* output) {
    if (!output->removed) {
        return;
    }
    river_output_v1_destroy(output->obj);
    wl_list_remove(&output->link);
    free(output);
}

static void window_handle_closed(void* data, struct river_window_v1* obj) {
    struct Window* window = data;
    window->closed = true;
}

static void window_handle_dimensions(void* data, struct river_window_v1* obj, int32_t width, int32_t height) {
    struct Window* window = data;
    window->width = width;
    window->height = height;
}

static void window_handle_pointer_move_requested(void* data, struct river_window_v1* obj, struct river_seat_v1* river_seat) {
    struct Window* window = data;
    window->pointer_move_requested = river_seat_v1_get_user_data(river_seat);
}

static void window_handle_pointer_resize_requested(void* data, struct river_window_v1* obj, struct river_seat_v1* river_seat, uint32_t edges) {
    struct Window* window = data;
    window->pointer_resize_requested = river_seat_v1_get_user_data(river_seat);
    window->pointer_resize_requested_edges = edges;
}

// Ignored events
static void window_handle_dimensions_hint(void* data, struct river_window_v1* obj, int32_t min_width, int32_t min_height, int32_t max_width, int32_t max_height) {
}
static void window_handle_app_id(void* data, struct river_window_v1* obj, const char* app_id) {
}
static void window_handle_title(void* data, struct river_window_v1* obj, const char* title) {
}
static void window_handle_parent(void* data, struct river_window_v1* obj, struct river_window_v1* parent) {
}
static void window_handle_decoration_hint(void* data, struct river_window_v1* obj, uint32_t hint) {
}
static void window_handle_show_window_menu_requested(void* data, struct river_window_v1* obj, int32_t x, int32_t y) {
}
static void window_handle_maximize_requested(void* data, struct river_window_v1* obj) {
}
static void window_handle_unmaximize_requested(void* data, struct river_window_v1* obj) {
}
static void window_handle_fullscreen_requested(void* data, struct river_window_v1* obj, struct river_output_v1* river_output) {
}
static void window_handle_exit_fullscreen_requested(void* data, struct river_window_v1* obj) {
}
static void window_handle_minimize_requested(void* data, struct river_window_v1* obj) {
}
static void window_handle_unreliable_pid(void* data, struct river_window_v1* obj, int32_t unreliable_pid) {
}
static void window_handle_presentation_hint(void* data, struct river_window_v1* obj, uint32_t hint) {
}
static void window_handle_identifier(void* data, struct river_window_v1* obj, const char* identifier) {
}

const struct river_window_v1_listener river_window_listener = {
    .closed = window_handle_closed,
    .dimensions_hint = window_handle_dimensions_hint,
    .dimensions = window_handle_dimensions,
    .app_id = window_handle_app_id,
    .title = window_handle_title,
    .parent = window_handle_parent,
    .decoration_hint = window_handle_decoration_hint,
    .pointer_move_requested = window_handle_pointer_move_requested,
    .pointer_resize_requested = window_handle_pointer_resize_requested,
    .show_window_menu_requested = window_handle_show_window_menu_requested,
    .maximize_requested = window_handle_maximize_requested,
    .unmaximize_requested = window_handle_unmaximize_requested,
    .fullscreen_requested = window_handle_fullscreen_requested,
    .exit_fullscreen_requested = window_handle_exit_fullscreen_requested,
    .minimize_requested = window_handle_minimize_requested,
    .unreliable_pid = window_handle_unreliable_pid,
    .presentation_hint = window_handle_presentation_hint,
    .identifier = window_handle_identifier,
};

static void window_maybe_destroy(struct Window* window) {
    if (!window->closed) {
        return;
    }

    struct Seat* seat;
    wl_list_for_each(seat, &wm.seats, link) {
        if (seat->focused == window) {
            seat->focused = NULL;
        }
        if (seat->op_window == window) {
            river_seat_v1_op_end(seat->obj);
            seat->op = SEAT_OP_NONE;
            seat->op_window = NULL;
        }
    }

    river_window_v1_destroy(window->obj);
    wl_list_remove(&window->link);
    free(window);
}

static void window_set_position(struct Window* window, int32_t x, int32_t y) {
    river_node_v1_set_position(window->node, x, y);
    window->x = x;
    window->y = y;
}

/// Add window to the start of the monitor's client list
static void attach(Window* w) {
    /* Insert at head using wl_list - Window's link gets added to monitor's clients list.
     * Note: Window struct needs a 'client_link' field for this. */
    wl_list_insert(&w->mon->clients, &w->link);
}

/// Remove window from the monitor's client list
static void detach(Window* w) {
    /* Remove from wl_list - just removes the link */
    wl_list_remove(&w->link);
}

/// Add window to the top of the monitor's stacking order
static void attachstack(Window* w) {
    /* Insert at head of stack list.
     * Note: Window struct needs a 'stack_link' field for this.
     * For now using same link - may need separate link field. */
    wl_list_insert(&w->mon->stack, &w->link);
}

/// Remove window from the monitor's stacking order
static void detachstack(Window* w) {
    /* Remove from stack list */
    wl_list_remove(&w->link);

    /* TODO: Handle focused window selection - needs sel field in Output */
}

/// This returns the next tiled client on the currently selected tag(s).
static Window* nexttiled(struct wl_list* start) {
    Window* w;

    /* Iterate through wl_list and find first non-floating visible window */
    wl_list_for_each(w, start, link) {
        if (!w->isfloating && ISVISIBLE(w))
            return w;
    }
    return NULL;
}

/// This function moves a client to the top of the tile stack, making it the new master window.
static void pop(Window* w) {
    /* Remove from current position and reattach at head.
     * This is used when focusing a window to make it "most recent" in the list.
     * In dwm, this is called when a window is focused to maintain MRU order. */
    detach(w);
    attach(w);

    // TODO Add focus and arrange?
}

/// Get the monitor that contains the given window
static Output* wintomon(Window* w) {
    return w->mon;
}

static void seat_focus(struct Seat* seat, struct Window* window);

/// Remove focus from a window
static void unfocus(Window* w, struct Seat* seat, int setfocus) {
    if (!w)
        return;

    if (setfocus)
        river_seat_v1_clear_focus(seat->obj);
}

/// This function gives input focus to a given client.
///
/// If the given client is NULL then focus will be given to the next visible client in the stacking
/// order. What this means is that the window that last had focus will receive input focus.
///
/// If there are no more visible clients then input focus will be reverted to the root window.
static void focus(Window* w, struct Seat* seat) {
    Output* m = seat->focused ? seat->focused->mon : NULL;

    /* If the given client is NULL, or it happens to not be visible, then search the first
     * visible client in the stacking order list. */
    if (!w || !ISVISIBLE(w)) {
        Window* iter;
        w = NULL;
        if (m) {
            wl_list_for_each(iter, &m->stack, link) {
                if (ISVISIBLE(iter)) {
                    w = iter;
                    break;
                }
            }
        }
    }

    /* If there is a selected client on the current monitor and that is different to the client
     * receiving focus, then we call unfocus on that client. */
    if (seat->focused && seat->focused != w)
        unfocus(seat->focused, seat, 0);

    /* If we are giving focus to a specific client then */
    if (w) {
        /* If the client has the urgency flag set then we remove that as the window is
         * receiving attention now. */
        // TODO: use seturgent
        if (w->isurgent)
            w->isurgent = 0;

        /* The detachstack and attachstack calls makes it so that the client becomes on top
         * of the stacking order, making it the last window to have received focus. */
        detachstack(w);
        attachstack(w);

        /* The call to setfocus handles the actual library calls to give the window input
         * focus. This is handled separately as setfocus can also be called from the focusin
         * event handler function. */
        seat_focus(seat, w);
    }
    /* It may be that there are no visible clients on the monitor, in which case we revert the
     * input focus back to the root window. */
    else {
        seat_focus(seat, NULL);
    }
    /* Set the selected client to be the one receiving focus, or to NULL in the event that there
     * are no visible clients. */
    seat->focused = w;

    // TODO
    // /* Finally update the bars on all monitors. This is in case the focus change resulted in the
    //  * selected monitor changing. */
    // drawbars();
}

/// User function to change focus between visible windows on the selected monitor.
/// TODO: direction -> arg
static void focusstack(struct Seat* seat, int direction) {
    Window* w = NULL;
    Window* i;

    /* Bail if there is no selected client on the current monitor, or if the selected client is
     * fullscreen and we disallow focus to drift from fullscreen windows. */
    /// TODO: full screen part
    if (!seat->focused)
        return;

    Output* m = seat->focused->mon;

    /* If the input value is positive then we move forward to find the next visible client. */
    if (direction > 0) {
        /* Next window */
        bool found = false;
        wl_list_for_each(i, &m->clients, link) {
            if (found && ISVISIBLE(i)) {
                w = i;
                break;
            }
            if (i == seat->focused)
                found = true;
        }
        /* Wrap around */
        if (!w) {
            wl_list_for_each(i, &m->clients, link) {
                if (ISVISIBLE(i)) {
                    w = i;
                    break;
                }
            }
        }
    } else {
        /* Previous window */
        wl_list_for_each(i, &m->clients, link) {
            if (i == seat->focused)
                break;
            if (ISVISIBLE(i))
                w = i;
        }
        /* Wrap to end */
        if (!w) {
            wl_list_for_each(i, &m->clients, link) {
                if (ISVISIBLE(i))
                    w = i;
            }
        }
    }

    if (w)
        focus(w, seat);
}

static void seat_pointer_move(struct Seat* seat, struct Window* window);
static void seat_pointer_resize(struct Seat* seat, struct Window* window, uint32_t edges);

static void window_manage(struct Window* window) {
    if (window->new) {
        window->new = false;
        window_set_position(window, 0, 0);
        river_window_v1_propose_dimensions(window->obj, 0, 0);
    }
    if (window->pointer_move_requested != NULL) {
        seat_pointer_move(window->pointer_move_requested, window);
        window->pointer_move_requested = NULL;
    }
    if (window->pointer_resize_requested != NULL) {
        seat_pointer_resize(window->pointer_resize_requested, window, window->pointer_resize_requested_edges);
        window->pointer_resize_requested = NULL;
    }
}

static void xkb_binding_handle_pressed(void* data, struct river_xkb_binding_v1* obj) {
    struct XkbBinding* binding = data;
    binding->seat->pending_action = binding->action;
}

static void xkb_binding_handle_released(void* data, struct river_xkb_binding_v1* obj) {
}

const struct river_xkb_binding_v1_listener river_xkb_binding_listener = {
    .pressed = xkb_binding_handle_pressed,
    .released = xkb_binding_handle_released,
};

static void xkb_binding_destroy(struct XkbBinding* binding) {
    river_xkb_binding_v1_destroy(binding->obj);
    wl_list_remove(&binding->link);
    free(binding);
}

static void xkb_binding_create(struct Seat* seat, uint32_t mods, xkb_keysym_t keysym, enum Action action) {
    struct XkbBinding* binding = calloc(1, sizeof(struct XkbBinding));
    binding->obj = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings_v1, seat->obj, keysym, mods);
    binding->seat = seat;
    binding->action = action;

    river_xkb_binding_v1_add_listener(binding->obj, &river_xkb_binding_listener, binding);
    river_xkb_binding_v1_enable(binding->obj);

    wl_list_insert(seat->xkb_bindings.prev, &binding->link);
}

static void pointer_binding_handle_pressed(void* data, struct river_pointer_binding_v1* obj) {
    struct PointerBinding* binding = data;
    binding->seat->pending_action = binding->action;
}

static void pointer_binding_handle_released(void* data, struct river_pointer_binding_v1* obj) {
}

const struct river_pointer_binding_v1_listener river_pointer_binding_listener = {
    .pressed = pointer_binding_handle_pressed,
    .released = pointer_binding_handle_released,
};

static void pointer_binding_destroy(struct PointerBinding* binding) {
    river_pointer_binding_v1_destroy(binding->obj);
    wl_list_remove(&binding->link);
    free(binding);
}

static void pointer_binding_create(struct Seat* seat, uint32_t mods, uint32_t button, enum Action action) {
    struct PointerBinding* binding = calloc(1, sizeof(struct PointerBinding));
    binding->obj = river_seat_v1_get_pointer_binding(seat->obj, button, mods);
    binding->seat = seat;
    binding->action = action;

    river_pointer_binding_v1_add_listener(binding->obj, &river_pointer_binding_listener, binding);
    river_pointer_binding_v1_enable(binding->obj);

    wl_list_insert(seat->pointer_bindings.prev, &binding->link);
}

static void seat_handle_removed(void* data, struct river_seat_v1* obj) {
    struct Seat* seat = data;
    seat->removed = true;
}

static void seat_handle_pointer_enter(void* data, struct river_seat_v1* obj, struct river_window_v1* river_window) {
    struct Seat* seat = data;
    seat->hovered = river_window_v1_get_user_data(river_window);
}

static void seat_handle_pointer_leave(void* data, struct river_seat_v1* obj) {
    struct Seat* seat = data;
    seat->hovered = NULL;
}

static void seat_handle_window_interaction(void* data, struct river_seat_v1* obj, struct river_window_v1* river_window) {
    struct Seat* seat = data;
    seat->interacted = river_window_v1_get_user_data(river_window);
}

static void seat_handle_op_delta(void* data, struct river_seat_v1* obj, int32_t dx, int32_t dy) {
    struct Seat* seat = data;
    seat->op_dx = dx;
    seat->op_dy = dy;
}

static void seat_handle_op_release(void* data, struct river_seat_v1* obj) {
    struct Seat* seat = data;
    seat->op_release = true;
}

// Ignored events
static void seat_handle_wl_seat(void* data, struct river_seat_v1* obj, uint32_t id) {
}
static void seat_handle_shell_surface_interaction(
    void* data, struct river_seat_v1* obj, struct river_shell_surface_v1* river_shell_surface) {
}
static void seat_handle_pointer_position(void* data, struct river_seat_v1* obj, int32_t x, int32_t y) {
}

const struct river_seat_v1_listener river_seat_listener = {
    .removed = seat_handle_removed,
    .wl_seat = seat_handle_wl_seat,
    .pointer_enter = seat_handle_pointer_enter,
    .pointer_leave = seat_handle_pointer_leave,
    .window_interaction = seat_handle_window_interaction,
    .shell_surface_interaction = seat_handle_shell_surface_interaction,
    .op_delta = seat_handle_op_delta,
    .op_release = seat_handle_op_release,
    .pointer_position = seat_handle_pointer_position,
};

static void seat_maybe_destroy(struct Seat* seat) {
    if (!seat->removed) {
        return;
    }

    struct XkbBinding *xkb_binding, *xkb_binding_tmp;
    wl_list_for_each_safe(xkb_binding, xkb_binding_tmp, &seat->xkb_bindings, link) {
        xkb_binding_destroy(xkb_binding);
    }

    struct PointerBinding *pointer_binding, *pointer_binding_tmp;
    wl_list_for_each_safe(pointer_binding, pointer_binding_tmp, &seat->pointer_bindings, link) {
        pointer_binding_destroy(pointer_binding);
    }

    river_seat_v1_destroy(seat->obj);
    wl_list_remove(&seat->link);
    free(seat);
}

static void seat_focus(struct Seat* seat, struct Window* window) {
    // Focus the top window (if any) when there is no explicit target.
    if (window == NULL && !wl_list_empty(&wm.windows)) {
        window = wl_container_of(wm.windows.prev, window, link);
    }

    if (seat->focused == window) {
        return;
    }

    if (window != NULL) {
        river_seat_v1_focus_window(seat->obj, window->obj);
        river_node_v1_place_top(window->node);
        wl_list_remove(&window->link);
        wl_list_insert(wm.windows.prev, &window->link);
    } else {
        river_seat_v1_clear_focus(seat->obj);
    }

    seat->focused = window;
}

static void seat_pointer_move(struct Seat* seat, struct Window* window) {
    seat_focus(seat, window);
    river_seat_v1_op_start_pointer(seat->obj);
    seat->op = SEAT_OP_MOVE;
    seat->op_window = window;
    seat->op_start_x = window->x;
    seat->op_start_y = window->y;
    seat->op_dx = 0;
    seat->op_dy = 0;
}

static void seat_pointer_resize(struct Seat* seat, struct Window* window, uint32_t edges) {
    seat_focus(seat, window);
    river_window_v1_inform_resize_start(window->obj);
    river_seat_v1_op_start_pointer(seat->obj);
    seat->op = SEAT_OP_RESIZE;
    seat->op_window = window;
    seat->op_edges = edges;
    seat->op_start_x = window->x;
    seat->op_start_y = window->y;
    seat->op_start_width = window->width;
    seat->op_start_height = window->height;
    seat->op_dx = 0;
    seat->op_dy = 0;
}

static void seat_action(struct Seat* seat, enum Action action) {
    switch (action) {
    case ACTION_NONE:
        break;
    case ACTION_SPAWN_FOOT:
        if (fork() == 0) {
            execlp("foot", "foot", (char*)0);
        }
        break;
    case ACTION_CLOSE:
        if (seat->focused != NULL) {
            river_window_v1_close(seat->focused->obj);
        }
        break;
    case ACTION_FOCUS_NEXT:
        if (!wl_list_empty(&wm.windows)) {
            // Focus the bottom window
            struct Window* window = wl_container_of(wm.windows.next, window, link);
            seat_focus(seat, window);
        }
        break;
    case ACTION_MOVE:
        if (seat->op == SEAT_OP_NONE && seat->hovered != NULL) {
            seat_pointer_move(seat, seat->hovered);
        }
        break;
    case ACTION_RESIZE:
        if (seat->op == SEAT_OP_NONE && seat->hovered != NULL) {
            seat_pointer_resize(seat, seat->hovered, RIVER_WINDOW_V1_EDGES_BOTTOM | RIVER_WINDOW_V1_EDGES_RIGHT);
        }
        break;
    case ACTION_EXIT:
        river_window_manager_v1_exit_session(window_manager_v1);
        break;
    }
}

static void seat_manage(struct Seat* seat) {
    if (seat->new) {
        seat->new = false;
        const uint32_t super = RIVER_SEAT_V1_MODIFIERS_MOD4;
        xkb_binding_create(seat, super, XKB_KEY_space, ACTION_SPAWN_FOOT);
        xkb_binding_create(seat, super, XKB_KEY_q, ACTION_CLOSE);
        xkb_binding_create(seat, super, XKB_KEY_n, ACTION_FOCUS_NEXT);
        xkb_binding_create(seat, super, XKB_KEY_Escape, ACTION_EXIT);
        pointer_binding_create(seat, super, BTN_LEFT, ACTION_MOVE);
        pointer_binding_create(seat, super, BTN_RIGHT, ACTION_RESIZE);
    }

    // If no window was interacted with in the current manage sequence,
    // intentionally pass NULL to ensure the window on top has focus.
    // This is necessary to handle new windows for example.
    seat_focus(seat, seat->interacted);
    seat->interacted = NULL;

    seat_action(seat, seat->pending_action);
    seat->pending_action = ACTION_NONE;

    switch (seat->op) {
    case SEAT_OP_NONE:
        break;
    case SEAT_OP_MOVE:
        if (seat->op_release) {
            river_seat_v1_op_end(seat->obj);
            seat->op = SEAT_OP_NONE;
            seat->op_window = NULL;
            break;
        }
        break;
    case SEAT_OP_RESIZE:
        if (seat->op_release) {
            river_window_v1_inform_resize_end(seat->op_window->obj);
            river_seat_v1_op_end(seat->obj);
            seat->op = SEAT_OP_NONE;
            seat->op_window = NULL;
            break;
        }
        int32_t width = seat->op_start_width;
        int32_t height = seat->op_start_height;
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
            width -= seat->op_dx;
        }
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_RIGHT) != 0) {
            width += seat->op_dx;
        }
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
            height -= seat->op_dy;
        }
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_BOTTOM) != 0) {
            height += seat->op_dy;
        }
        river_window_v1_propose_dimensions(seat->op_window->obj, width > 1 ? width : 1, height > 1 ? height : 1);
        break;
    }
    seat->op_release = false;
}

static void seat_render(struct Seat* seat) {
    switch (seat->op) {
    case SEAT_OP_NONE:
        break;
    case SEAT_OP_MOVE:
        window_set_position(seat->op_window, seat->op_start_x + seat->op_dx, seat->op_start_y + seat->op_dy);
        break;
    case SEAT_OP_RESIZE:;
        int32_t x = seat->op_start_x;
        int32_t y = seat->op_start_y;
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
            x += seat->op_start_width - seat->op_window->width;
        }
        if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
            y += seat->op_start_height - seat->op_window->height;
        }
        window_set_position(seat->op_window, x, y);
        break;
    }
}

static void wm_handle_unavailable(void* data, struct river_window_manager_v1* obj) {
    fprintf(stderr, "error: another window manager is already running\n");
    exit(1);
}

static void wm_handle_finished(void* data, struct river_window_manager_v1* obj) {
    exit(0);
}

static void wm_handle_manage_start(void* data, struct river_window_manager_v1* obj) {
    // Destroy closed windows and removed outputs/seats
    struct Output *output, *output_tmp;
    wl_list_for_each_safe(output, output_tmp, &wm.outputs, link) {
        output_maybe_destroy(output);
    }
    struct Window *window, *window_tmp;
    wl_list_for_each_safe(window, window_tmp, &wm.windows, link) {
        window_maybe_destroy(window);
    }
    struct Seat *seat, *seat_tmp;
    wl_list_for_each_safe(seat, seat_tmp, &wm.seats, link) {
        seat_maybe_destroy(seat);
    }

    // Carry out window management policy
    wl_list_for_each(window, &wm.windows, link) {
        window_manage(window);
    }
    wl_list_for_each(seat, &wm.seats, link) {
        seat_manage(seat);
    }

    river_window_manager_v1_manage_finish(window_manager_v1);
}

static void wm_handle_render_start(void* data, struct river_window_manager_v1* obj) {
    struct Seat* seat;
    wl_list_for_each(seat, &wm.seats, link) {
        seat_render(seat);
    }

    river_window_manager_v1_render_finish(window_manager_v1);
}

static void wm_handle_window(void* data, struct river_window_manager_v1* obj, struct river_window_v1* river_window) {
    struct Window* window = calloc(1, sizeof(struct Window));
    window->obj = river_window;
    window->node = river_window_v1_get_node(window->obj);
    window->new = true;

    river_window_v1_add_listener(window->obj, &river_window_listener, window);

    wl_list_insert(wm.windows.prev, &window->link);
}

static void wm_handle_output(void* data, struct river_window_manager_v1* obj, struct river_output_v1* river_output) {
    struct Output* output = calloc(1, sizeof(struct Output));
    output->obj = river_output;

    river_output_v1_add_listener(output->obj, &river_output_listener, output);

    wl_list_insert(wm.outputs.prev, &output->link);
}

static void wm_handle_seat(void* data, struct river_window_manager_v1* obj, struct river_seat_v1* river_seat) {
    struct Seat* seat = calloc(1, sizeof(struct Seat));
    seat->obj = river_seat;
    seat->new = true;
    wl_list_init(&seat->xkb_bindings);
    wl_list_init(&seat->pointer_bindings);

    river_seat_v1_add_listener(seat->obj, &river_seat_listener, seat);

    wl_list_insert(wm.seats.prev, &seat->link);
}

// Ignored events
static void wm_handle_session_locked(void* data, struct river_window_manager_v1* obj) {
}
static void wm_handle_session_unlocked(void* data, struct river_window_manager_v1* obj) {
}

static const struct river_window_manager_v1_listener wm_listener = {
    .unavailable = wm_handle_unavailable,
    .finished = wm_handle_finished,
    .manage_start = wm_handle_manage_start,
    .render_start = wm_handle_render_start,
    .session_locked = wm_handle_session_locked,
    .session_unlocked = wm_handle_session_unlocked,
    .window = wm_handle_window,
    .output = wm_handle_output,
    .seat = wm_handle_seat,
};

static void wm_init(void) {
    wl_list_init(&wm.outputs);
    wl_list_init(&wm.windows);
    wl_list_init(&wm.seats);
}

static void handle_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (strcmp(interface, river_window_manager_v1_interface.name) == 0) {
        if (version >= 4) {
            window_manager_v1 = wl_registry_bind(registry, name, &river_window_manager_v1_interface, 4);
        }
    } else if (strcmp(interface, river_xkb_bindings_v1_interface.name) == 0) {
        xkb_bindings_v1 = wl_registry_bind(registry, name, &river_xkb_bindings_v1_interface, 1);
    }
}

static void handle_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

int main(void) {
    struct wl_display* display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to connect to Wayland server\n");
        return 1;
    }

    // Avoid passing WAYLAND_DEBUG on to our children.
    // It only matters if it's set when the display is created.
    unsetenv("WAYLAND_DEBUG");

    // Ensure children are automatically reaped.
    signal(SIGCHLD, SIG_IGN);

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "roundtrip failed\n");
        return 1;
    }

    if (window_manager_v1 == NULL || xkb_bindings_v1 == NULL) {
        fprintf(stderr, "river_window_manager_v1 or river_xkb_bindings_v1 "
                        "not supported by the Wayland server\n");
        return 1;
    }

    wm_init();

    river_window_manager_v1_add_listener(window_manager_v1, &wm_listener, NULL);

    while (true) {
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "dispatch failed\n");
            return 1;
        }
    }

    return 0;
}
