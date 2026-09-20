#ifndef ANVLH
#define ANVLH

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <river-input-management-v1-client-protocol.h>
#include <river-layer-shell-v1-client-protocol.h>
#include <river-window-management-v1-client-protocol.h>
#include <river-xkb-bindings-v1-client-protocol.h>
#include <river-xkb-config-v1-client-protocol.h>

#include <wlr-layer-shell-unstable-v1-client-protocol.h>

typedef struct WlOutput WlOutput;
typedef struct Window Window;
typedef struct Output Output;
typedef struct Layout Layout;
typedef struct Node Node;
typedef struct Seat Seat;
typedef struct Tag Tag;

struct Window {
    struct river_window_v1* river_window;
    struct river_node_v1* river_node;
    struct wl_list link;

    char* title;

    int x;
    int y;

    int width;
    int height;

    Node* node;
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

    Window* window;

    Node* first;
    Node* second;
    Node* parent;

    Tag* tag;
};

struct Tag {
    int n;
    const char* sym;

    Node* root;
    Node* focused;

    Layout* lt;
};

struct Output {
    struct river_output_v1* river_output;
    struct river_layer_shell_output_v1* river_layer_shell;
    struct wl_list link;

    int x;
    int y;

    int width;
    int height;

    uint32_t seltag;
    Tag* tags[9];
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

    Window* focused;

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

extern struct fcft_font* fcft_font;

extern struct wl_shm* shm;
extern struct wl_compositor* compositor;
extern struct zwlr_layer_shell_v1* zwlr_layer_shell;
extern struct xkb_context* xkb_context;
extern struct river_xkb_config_v1* xkb_config;
extern struct river_xkb_keymap_v1* xkb_keymap;
extern struct river_layer_shell_v1* layer_shell;
extern struct river_xkb_bindings_v1* xkb_bindings;
extern struct river_input_manager_v1* input_manager;
extern struct river_window_manager_v1* window_manager;

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
void focus_next(Seat* seat, Arg* arg);
void focus_prev(Seat* esat, Arg* arg);
void set_layout(Seat* seat, Arg* arg);
void spawn(Seat* seat, Arg* arg);
void view(Seat* seat, Arg* arg);
void tag(Seat* seat, Arg* arg);

Node* create_node(Tag* tag, Window* window, Node* parent);
void insert_node(Window* window, Node* root, Node* ref);
void remove_node(Node* node);
void propogate_layout(Node* root);

void tile(Output* output);
void monocle(Output* output);
void window_set_position(Window* window, int x, int y);
void window_set_dimensions(Window* window, int width, int height);
void manage_seat(Seat* seat);
void anvl_add_window(Window* window);
void anvl_remove_window(Window* window);
void anvl_add_output(Output* output);
void anvl_remove_output(Output* output);
void anvl_output_position(Output* output, int x, int y);
void anvl_output_dimensions(Output* output, int width, int height);
void anvl_manage(void);
bool set_font_scale(int scale);
void render_bar(WlOutput* output);

extern const struct wl_registry_listener registry_listener;

#endif /* ANVLH */
