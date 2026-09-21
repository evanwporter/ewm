#ifndef ANVLH
#define ANVLH

#include <sys/types.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#define SELECTED_WORKSPACE(M) ((M)->workspaces[(M)->selected_workspace - 1])

struct river_window_v1;
struct river_node_v1;
struct river_output_v1;
struct river_layer_shell_output_v1;
struct river_seat_v1;
struct river_xkb_binding_v1;
struct river_pointer_binding_v1;
struct zwlr_layer_surface_v1;

typedef struct WlOutput WlOutput;
typedef struct Client Client;
typedef struct Output Output;
typedef struct Layout Layout;
typedef struct Node Node;
typedef struct Seat Seat;
typedef struct Workspace Workspace;

struct Client {
    struct river_window_v1* river_window;
    struct river_node_v1* river_node;
    struct wl_list link;

    /// The name holds the window title.
    char* name;
    char* title;

    /// The client x, y coordinates and size (width, height).
    int x, y, w, h;
    int width, height;

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

    /// The workspace the client is attached too
    unsigned int workspace;

    /// Non-zero scratchpad identifier; retained while a floating pad moves workspaces.
    unsigned int scratchpad;

    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
    pid_t pid;

    /// The next client in the client list, which is a linked list. The client list controls the
    /// order in which clients are tiled.
    Client* next;

    /* The leaf node representing this client in its monitor's tree layout. */
    Node* node;

    /* The next client in the stacking order list, which is also a linked list. The stacking
     * order indicates which window is on top of others as well as the order in which clients
     * had focus. */
    Client* snext;

    Client* swallowing;

    /// The output this client belongs to.
    Output* mon;

    // /// The managed window that this client represents.
    // Client win;

    /// The icon to display in the tabline / window titles
    char* icon;
};

typedef enum { HORIZONTAL,
               VERTICAL,
               UNSET } split_type_t;

struct Node {
    split_type_t split_type;
    double split_ratio;

    int x;
    int y;

    int width;
    int height;

    Client* window;

    Node* first;
    Node* second;
    Node* parent;

    Workspace* workspace;
};

struct Workspace {
    int n;
    const char* sym;

    Node* root;
    Node* focused;

    Layout* lt;
    float master_ratio;
    unsigned int master_count;

    /* This represents the number of clients that are to be tiled in the master area. This has
     * no upper limit but cannot be less than 0. The default value is configured in the
     * configuration file and the value is adjusted via the incnmaster function. */
    //  Default nmaster = 1:
    // ┌───────────┬────┐
    // │           │ C2 │
    // │    C1     ├────┤
    // │  (master) │ C3 │
    // │           ├────┤
    // │           │ C4 │
    // └───────────┴────┘
    //
    // With nmaster = 2:
    // ┌───────────┬────┐
    // │    C1     │ C3 │
    // │  (master) ├────┤
    // ├───────────┤ C4 │
    // │    C2     ├────┤
    // │ (also     │ C5 │
    // │  master)  │    │
    // └───────────┴────┘
    /// Number of windows in master area
    int nmaster;

    /// What percentage of the screen master gets
    float mfact;

    /* The sellt variable is either 0 or 1 and represents the currently selected layout. This
     * follows the same mechanism as seltags above giving patterns such a:
     *
     *    m->lt[m->sellt]
     *    selmon->lt[selmon->sellt]
     *    c->mon->lt[c->mon->sellt]
     */
    unsigned int sellt;

    /* This array holds the previous and current layout for the monitor, the index of which is
     * indicated by the sellt variable. */
    const Layout* layouts[2];

    /* This holds the layout symbol text, typically as defined in the layouts array. This is
     * used when drawing the layout symbol on the bar. The reason why this is defined for the
     * monitor rather than simply using the layout symbol as defined in the layouts array is
     * that some layouts, like the monocle layout for example, may alter the layout symbol
     * depending on how many clients are present. */
    char ltsymbol[16];

    /* Internal flag indicating whether the bar is shown or not. */
    int showbar;

    /* Internal flag indicating whether the bar is shown at the top or at the bottom. */
    int topbar;

};

struct Output {
    struct river_output_v1* river_output;
    struct river_layer_shell_output_v1* river_layer_shell;
    struct wl_list link;

    /* These variables represents the position and dimensions of the monitor.
     *    mx - monitor position on the x-axis
     *    my - monitor position on the y-axis
     *    mw - the monitor's width
     *    mh - the monitor's height
     */
    int mx, my, mw, mh;

    int width;
    int height;

    /* The by variable defines the bar windows position on the y axis and this is set in the
     * updatebarpos function. */
    int by; /* bar geometry */
    int btw; /* width of tasks portion of bar */
    int bt; /* number of tasks */

    /* These variables represents the position and dimensions of the window area, as in the part
     * of the monitor where windows are tiled. This is the space of the monitor excluding the
     * bar window. These are set in the updatebarpos function.
     *    wx - window area position on the x-axis
     *    wy - window area position on the y-axis
     *    ww - the window area's width
     *    wh - the window area's height
     */
    int wx, wy, ww, wh;

    /// Workspace IDs are one-based; the current and previous values support toggling.
    unsigned int selected_workspace;
    unsigned int previous_workspace;

    int hidsel;

    /* The client list. This represents the start of a linked list of clients which determines
     * the order in which clients are tiled. */
    Client* clients;

    /* This represents the monitor's selected client. */
    Client* sel;

    /* The stacking order list. This represents the order in which client windows are stacked on
     * top of each other, as well as the order in which clients had last focus. */
    Client* stack;

    /* Monitors are also managed as a linked list with the mons variable referring to the first
     * monitor. The next variable on the monitor refers to the next monitor in the list. */
    Output* next;

    // /// The root of the tree tile display
    // TreeNode *root;

    /* This is the bar window which is used to draw the bar. Each monitor has their own bar. */
    Client barwin;

    Workspace* workspaces[9];
};

struct WlOutput {
    struct wl_output* wl_output;
    struct wl_list link;

    Output* output;

    int width;
    int height;
    int scale;

    bool done;
    bool configured;
    uint32_t name;

    struct wl_surface* surface;
    struct zwlr_layer_surface_v1* layer_surface;
};

struct Seat {
    struct river_seat_v1* river_seat;
    struct wl_list link;

    Client* focused;

    struct wl_list keys;
    struct wl_list buttons;
};

typedef struct {
    struct wl_list windows;
    struct wl_list outputs;
    struct wl_list seats;
    struct wl_list keyboards;
    struct wl_list wl_outputs;
} WindowManager;

struct Layout {
    char* symbol;
    void (*manage)(Output*);
};

typedef union {
    int i;
    void* v;
    float f;
    uint32_t u;
} Arg;

typedef struct {
    struct river_xkb_binding_v1* river_xkb_binding;
    struct wl_list link;

    Seat* seat;

    void (*func)(Seat* seat, Arg* arg);
    Arg* arg;
} Key;

typedef struct {
    struct river_pointer_binding_v1* river_pointer_binding;
    struct wl_list link;

    Seat* seat;

    bool pressed;

    void (*func)(Seat* seat, Arg* arg);
    Arg* arg;
} Button;

extern WindowManager anvl;
extern Output* selmon;

typedef struct {
    uint32_t mods;
    xkb_keysym_t key;
    void (*func)(Seat* seat, Arg* arg);
    Arg arg;
} Keys;

void destroy_window(Seat* seat, Arg* arg);
void focus_next_mon(Seat* seat, Arg* arg);
void focus_prev_mon(Seat* seat, Arg* arg);
void tag_next_mon(Seat* seat, Arg* arg);
void tag_prev_mon(Seat* seat, Arg* arg);
void exit_session(Seat* seat, Arg* arg);
void focus_client(Seat* seat, Client* client);
void focusstack(Seat* seat, Arg* arg);
void focus_next(Seat* seat, Arg* arg);
void focus_prev(Seat* esat, Arg* arg);
void setlayout(Seat* seat, Arg* arg);
void setmfact(Seat* seat, Arg* arg);
void incnmaster(Seat* seat, Arg* arg);
void spawn(Seat* seat, Arg* arg);
void viewworkspace(Seat* seat, Arg* arg);
void sendtoworkspace(Seat* seat, Arg* arg);
void movetoworkspace(Seat* seat, Arg* arg);

Node* create_node(Workspace* workspace, Client* window, Node* parent);
void insert_node(Client* window, Node* root, Node* ref);
void remove_node(Node* node);
void propogate_layout(Node* root);

void tile(Output* output);
void monocle(Output* output);
void manage_seat(Seat* seat);
void anvl_add_window(Client* window);
void anvl_remove_window(Client* window);
void anvl_add_output(Output* output);
void anvl_remove_output(Output* output);
void anvl_output_position(Output* output, int x, int y);
void anvl_output_dimensions(Output* output, int width, int height);
void anvl_manage(void);

#endif /* ANVLH */
