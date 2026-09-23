#ifndef ANVLH
#define ANVLH

#include <stdint.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

struct river_window_v1;
struct river_node_v1;
struct river_output_v1;
struct river_layer_shell_output_v1;
struct river_seat_v1;
struct river_xkb_binding_v1;
struct river_pointer_binding_v1;
struct zwlr_layer_surface_v1;

typedef struct WlOutput WlOutput;
typedef struct Window Window;
typedef struct Output Output;
typedef struct Layout Layout;
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

    uint32_t tag;
    Output* mon;
};

struct Tag {
    int n;
    const char* sym;

    Window* focused;

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

typedef struct {
    uint32_t mods;
    xkb_keysym_t key;
    void (*func)(Seat* seat, Arg* arg);
    Arg arg;
} Keys;

void destroy_window(Seat* seat, Arg* arg);
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

void tile(Output* output);
void monocle(Output* output);
void manage_seat(Seat* seat);
void anvl_add_window(Window* window);
void anvl_remove_window(Window* window);
void anvl_add_output(Output* output);
void anvl_remove_output(Output* output);
void anvl_output_position(Output* output, int x, int y);
void anvl_output_dimensions(Output* output, int width, int height);
void anvl_manage(void);

#endif /* ANVLH */
