#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 202405L
#endif

#include <sys/mman.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "anvl.h"
#include "config.h"

#define MAX(A, B) (A > B ? A : B)
#define LENGTH(A) (sizeof A / sizeof A[0])

void river_output_v1_removed(void* data, struct river_output_v1* obj) {
    Output* output = data;

    river_layer_shell_output_v1_destroy(output->river_layer_shell);
    river_output_v1_destroy(output->river_output);
    anvl_remove_output(output);
}

void river_output_v1_wl_output(void* data, struct river_output_v1* obj, uint32_t name) {
    Output* output = data;

    WlOutput* wl_output;
    wl_list_for_each(wl_output, &anvl.wl_outputs, link) {
        if (wl_output->name == name) {
            wl_output->output = output;
            wl_output->done = true;
            if (wl_output->configured)
                render_bar(wl_output);
        }
    }
}

void river_output_v1_position(void* data, struct river_output_v1* obj, int32_t x, int32_t y) {
    Output* output = data;

    anvl_output_position(output, x, y);
}

void river_output_v1_dimensions(void* data, struct river_output_v1* obj, int32_t width, int32_t height) {
    Output* output = data;

    anvl_output_dimensions(output, width, height);
}

const struct river_output_v1_listener output_listener = {
    .removed = river_output_v1_removed,
    .wl_output = river_output_v1_wl_output,
    .position = river_output_v1_position,
    .dimensions = river_output_v1_dimensions,
};

void river_window_v1_closed(void* data, struct river_window_v1* obj) {
    Window* window = data;

    anvl_remove_window(window);

    river_window_v1_destroy(window->river_window);
    free(window->title);
    free(window);
}

void river_window_v1_dimensions_hint(void* data, struct river_window_v1* obj, int32_t min_width, int32_t min_height, int32_t max_width, int32_t max_height) { }

void river_window_v1_dimensions(void* data, struct river_window_v1* obj, int32_t width, int32_t height) {
    struct Window* window = data;
    window->width = width;
    window->height = height;
}

void river_window_v1_app_id(void* data, struct river_window_v1* obj, const char* app_id) { }
void river_window_v1_title(void* data, struct river_window_v1* obj, const char* title) {
    Window* window = data;

    free(window->title);
    window->title = title == NULL ? NULL : strdup(title);
}
void river_window_v1_parent(void* data, struct river_window_v1* obj, struct river_window_v1* parent) { }
void river_window_v1_decoration_hint(void* data, struct river_window_v1* obj, uint32_t hint) { }
void river_window_v1_pointer_move_requested(void* data, struct river_window_v1* obj, struct river_seat_v1* river_seat) { }
void river_window_v1_pointer_resize_requested(void* data, struct river_window_v1* obj, struct river_seat_v1* river_seat, uint32_t edges) { }
void river_window_v1_show_window_menu_requested(void* data, struct river_window_v1* obj, int32_t x, int32_t y) { }
void river_window_v1_maximize_requested(void* data, struct river_window_v1* obj) { }
void river_window_v1_unmaximize_requested(void* data, struct river_window_v1* obj) { }
void river_window_v1_fullscreen_requested(
    void* data, struct river_window_v1* obj, struct river_output_v1* river_output) { }
void river_window_v1_exit_fullscreen_requested(void* data, struct river_window_v1* obj) { }
void river_window_v1_minimize_requested(void* data, struct river_window_v1* obj) { }
void river_window_v1_unreliable_pid(void* data, struct river_window_v1* obj, int32_t unreliable_pid) { }
void river_window_v1_presentation_hint(void* data, struct river_window_v1* obj, uint32_t hint) { }
void river_window_v1_identifier(void* data, struct river_window_v1* obj, const char* indentifier) { }

const struct river_window_v1_listener window_listener = {
    .closed = river_window_v1_closed,
    .dimensions_hint = river_window_v1_dimensions_hint,
    .dimensions = river_window_v1_dimensions,
    .app_id = river_window_v1_app_id,
    .title = river_window_v1_title,
    .parent = river_window_v1_parent,
    .decoration_hint = river_window_v1_decoration_hint,
    .pointer_move_requested = river_window_v1_pointer_move_requested,
    .pointer_resize_requested = river_window_v1_pointer_resize_requested,
    .show_window_menu_requested = river_window_v1_show_window_menu_requested,
    .maximize_requested = river_window_v1_maximize_requested,
    .unmaximize_requested = river_window_v1_unmaximize_requested,
    .fullscreen_requested = river_window_v1_fullscreen_requested,
    .exit_fullscreen_requested = river_window_v1_exit_fullscreen_requested,
    .minimize_requested = river_window_v1_minimize_requested,
    .unreliable_pid = river_window_v1_unreliable_pid,
    .presentation_hint = river_window_v1_presentation_hint,
    .identifier = river_window_v1_identifier,
};

void window_set_position(Window* window, int x, int y) {
    window->x = x;
    window->y = y;
    river_node_v1_set_position(window->river_node, window->x, window->y);
}

void window_set_dimensions(Window* window, int width, int height) {
    window->width = width;
    window->height = height;
    river_window_v1_propose_dimensions(window->river_window, window->width, window->height);
}

void river_xkb_binding_v1_pressed(void* data, struct river_xkb_binding_v1* obj) {
    Key* key = data;
    key->func(key->seat, key->arg);
}

void river_xkb_binding_v1_released(void* data, struct river_xkb_binding_v1* obj) { }

const struct river_xkb_binding_v1_listener xkb_binding_listener = {
    .pressed = river_xkb_binding_v1_pressed,
    .released = river_xkb_binding_v1_released,
};

void xkb_binding_create(Seat* seat, uint32_t modifiers, xkb_keysym_t keysym, void (*func)(Seat* seat, Arg* arg), Arg* arg) {
    Key* key = calloc(1, sizeof(Key));
    key->river_xkb_binding = river_xkb_bindings_v1_get_xkb_binding(
        xkb_bindings, seat->river_seat, keysym, modifiers);
    key->seat = seat;
    key->func = func;
    key->arg = arg;

    river_xkb_binding_v1_add_listener(key->river_xkb_binding, &xkb_binding_listener, key);
    river_xkb_binding_v1_enable(key->river_xkb_binding);

    wl_list_insert(&seat->keys, &key->link);
}

void xkb_binding_destroy(Key* key) {
    river_xkb_binding_v1_destroy(key->river_xkb_binding);
    wl_list_remove(&key->link);
    free(key);
}

void river_pointer_binding_v1_pressed(void* data, struct river_pointer_binding_v1* obj) {
    Button* button = data;

    button->pressed = true;
}

void river_pointer_binding_v1_released(void* data, struct river_pointer_binding_v1* obj) {
    Button* button = data;

    button->pressed = false;
}

const struct river_pointer_binding_v1_listener pointer_binding_listener = {
    .pressed = river_pointer_binding_v1_pressed,
    .released = river_pointer_binding_v1_released,
};

void pointer_binding_create(Seat* seat, uint32_t modifiers, uint32_t ibutton, void (*func)(Seat* seat, Arg* arg), Arg* arg) {
    Button* button = calloc(1, sizeof(Button));
    button->river_pointer_binding = river_seat_v1_get_pointer_binding(seat->river_seat, ibutton, modifiers);
    button->seat = seat;
    button->func = func;
    button->arg = arg;

    river_pointer_binding_v1_add_listener(button->river_pointer_binding, &pointer_binding_listener, button);
    river_pointer_binding_v1_enable(button->river_pointer_binding);

    wl_list_insert(&seat->buttons, &button->link);
}

void pointer_binding_destroy(Button* button) {
    river_pointer_binding_v1_destroy(button->river_pointer_binding);
    wl_list_remove(&button->link);
    free(button);
}

void river_seat_v1_removed(void* data, struct river_seat_v1* obj) {
    Seat* seat = data;

    Key *key, *key_tmp;
    wl_list_for_each_safe(key, key_tmp, &seat->keys, link) {
        xkb_binding_destroy(key);
    }

    Button *button, *button_tmp;
    wl_list_for_each_safe(button, button_tmp, &seat->buttons, link) {
        pointer_binding_destroy(button);
    }

    river_seat_v1_destroy(seat->river_seat);
    wl_list_remove(&seat->link);
    free(seat);
}

void river_seat_v1_wl_seat(void* data, struct river_seat_v1* obj, uint32_t id) {
}

void river_seat_v1_pointer_enter(void* data, struct river_seat_v1* obj, struct river_window_v1* river_window) {
    Seat* seat = data;
    Window* window = river_window_v1_get_user_data(river_window);

    seat->focused = window;
}

void river_seat_v1_pointer_leave(void* data, struct river_seat_v1* obj) { }

void river_seat_v1_window_interaction(void* data, struct river_seat_v1* obj, struct river_window_v1* river_window) {
    Seat* seat = data;
    Window* window = seat->focused;

    window = river_window_v1_get_user_data(river_window);

    seat->focused = window;
}

void river_seat_v1_shell_surface_interaction(
    void* data, struct river_seat_v1* obj, struct river_shell_surface_v1* river_shell_surface) { }
void river_seat_v1_op_delta(void* data, struct river_seat_v1* obj, int32_t dx, int32_t dy) { }
void river_seat_v1_op_release(void* data, struct river_seat_v1* obj) {
}
void river_seat_v1_pointer_position(void* data, struct river_seat_v1* obj, int32_t x, int32_t y) {
    Output* output;
    wl_list_for_each(output, &anvl.outputs, link) {
        if (x >= output->x && x < output->x + output->width && y >= output->y && y < output->y + output->height) {
            selmon = output;
            river_layer_shell_output_v1_set_default(output->river_layer_shell);
            return;
        }
    }

    selmon = NULL;
}

const struct river_seat_v1_listener seat_listener = {
    .removed = river_seat_v1_removed,
    .wl_seat = river_seat_v1_wl_seat,
    .pointer_enter = river_seat_v1_pointer_enter,
    .pointer_leave = river_seat_v1_pointer_leave,
    .window_interaction = river_seat_v1_window_interaction,
    .shell_surface_interaction = river_seat_v1_shell_surface_interaction,
    .op_delta = river_seat_v1_op_delta,
    .op_release = river_seat_v1_op_release,
    .pointer_position = river_seat_v1_pointer_position,
};

void river_layer_shell_output_v1_non_exclusive_area(
    void* data, struct river_layer_shell_output_v1* handle, int32_t x, int32_t y, int32_t width, int32_t height) { }

const struct river_layer_shell_output_v1_listener layer_shell_output_listener = {
    .non_exclusive_area = river_layer_shell_output_v1_non_exclusive_area,
};

const struct wl_callback_listener wl_surface_frame_listener;
void river_window_manager_v1_unavailable(void* data, struct river_window_manager_v1* obj) {
    fprintf(stderr, "error: Unavailable.\n");
    exit(1);
}

void river_window_manager_v1_finished(void* data, struct river_window_manager_v1* obj) {
    exit(0);
}

void river_window_manager_v1_manage_start(void* data, struct river_window_manager_v1* obj) {
    anvl_manage();

    river_window_manager_v1_manage_finish(window_manager);
}

void river_window_manager_v1_render_start(void* data, struct river_window_manager_v1* obj) {
    WlOutput* output;
    wl_list_for_each(output, &anvl.wl_outputs, link) { render_bar(output); }

    river_window_manager_v1_render_finish(window_manager);
}

void wl_surface_frame_done(void* data, struct wl_callback* cb, uint32_t time) {
    wl_callback_destroy(cb);

    WlOutput* output = data;
    cb = wl_surface_frame(output->surface);
    wl_callback_add_listener(cb, &wl_surface_frame_listener, output);

    render_bar(output);
}

const struct wl_callback_listener wl_surface_frame_listener = {
    .done = wl_surface_frame_done,
};

void zwlr_layer_surface_v1_configure(
    void* data, struct zwlr_layer_surface_v1* zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height) {
    WlOutput* output = data;

    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);

    if (output->done && output->width == width && output->height == height) {
        wl_surface_commit(output->surface);
        return;
    }

    output->width = width;
    output->height = height;
    output->configured = true;
    if (output->done)
        render_bar(output);
}

void zwlr_layer_surface_v1_closed(
    void* data, struct zwlr_layer_surface_v1* zwlr_layer_surface_v1) { }

const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = zwlr_layer_surface_v1_configure,
    .closed = zwlr_layer_surface_v1_closed,
};

void river_window_manager_v1_session_locked(
    void* data, struct river_window_manager_v1* obj) { }
void river_window_manager_v1_session_unlocked(
    void* data, struct river_window_manager_v1* obj) { }

void river_window_manager_v1_window(void* data, struct river_window_manager_v1* obj, struct river_window_v1* river_window) {
    Window* window = calloc(1, sizeof(Window));
    window->river_window = river_window;
    window->river_node = river_window_v1_get_node(window->river_window);

    river_window_v1_add_listener(window->river_window, &window_listener, window);
    anvl_add_window(window);
}

void river_window_manager_v1_output(void* data, struct river_window_manager_v1* obj, struct river_output_v1* river_output) {
    Output* output = calloc(1, sizeof(Output));
    output->river_output = river_output;
    output->river_layer_shell = river_layer_shell_v1_get_output(layer_shell, river_output);
    river_output_v1_add_listener(output->river_output, &output_listener, output);
    river_layer_shell_output_v1_add_listener(
        output->river_layer_shell, &layer_shell_output_listener, output);
    anvl_add_output(output);
    if (selmon == output)
        river_layer_shell_output_v1_set_default(output->river_layer_shell);
}

void river_window_manager_v1_seat(void* data, struct river_window_manager_v1* obj, struct river_seat_v1* river_seat) {
    Seat* seat = calloc(1, sizeof(Seat));
    seat->river_seat = river_seat;
    seat->focused = NULL;

    wl_list_init(&seat->keys);
    wl_list_init(&seat->buttons);

    river_seat_v1_add_listener(seat->river_seat, &seat_listener, seat);
    wl_list_insert(&anvl.seats, &seat->link);

    for (int i = 0; i < LENGTH(keybinds); i++) {
        xkb_binding_create(seat, keybinds[i].mods, keybinds[i].key, keybinds[i].func, &keybinds[i].arg);
    }
}

const struct river_window_manager_v1_listener window_manager_listener = {
    .unavailable = river_window_manager_v1_unavailable,
    .finished = river_window_manager_v1_finished,
    .manage_start = river_window_manager_v1_manage_start,
    .render_start = river_window_manager_v1_render_start,
    .session_locked = river_window_manager_v1_session_locked,
    .session_unlocked = river_window_manager_v1_session_unlocked,
    .window = river_window_manager_v1_window,
    .output = river_window_manager_v1_output,
    .seat = river_window_manager_v1_seat,
};

void river_input_manager_v1_finished(
    void* data, struct river_input_manager_v1* river_input_manager_v1) { }
void river_input_manager_v1_input_device(
    void* data, struct river_input_manager_v1* river_input_manager_v1, struct river_input_device_v1* id) { }

const struct river_input_manager_v1_listener input_manager_listener = {
    .finished = river_input_manager_v1_finished,
    .input_device = river_input_manager_v1_input_device,
};

void river_input_device_v1_removed(
    void* data, struct river_input_device_v1* river_input_device_v1) { }
void river_input_device_v1_type(
    void* data, struct river_input_device_v1* river_input_device_v1, uint32_t type) { }
void river_input_device_v1_name(
    void* data, struct river_input_device_v1* river_input_device_v1, const char* name) { }

const struct river_input_device_v1_listener input_device_listener = {
    .removed = river_input_device_v1_removed,
    .type = river_input_device_v1_type,
    .name = river_input_device_v1_name,
};

typedef struct {
    struct river_xkb_keyboard_v1* river_xkb_keyboard;

    struct wl_list link;
} Keyboard;

void river_xkb_keyboard_v1_removed(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1) {
    Keyboard* keyboard = data;

    river_xkb_keyboard_v1_destroy(keyboard->river_xkb_keyboard);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

void river_xkb_keyboard_v1_input_device(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1, struct river_input_device_v1* device) { }
void river_xkb_keyboard_v1_layout(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1, uint32_t index, const char* name) { }
void river_xkb_keyboard_v1_capslock_enabled(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1) { }
void river_xkb_keyboard_v1_capslock_disabled(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1) { }
void river_xkb_keyboard_v1_numlock_enabled(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1) { }
void river_xkb_keyboard_v1_numlock_disabled(
    void* data, struct river_xkb_keyboard_v1* river_xkb_keyboard_v1) { }

const struct river_xkb_keyboard_v1_listener xkb_keyboard_listener = {
    .removed = river_xkb_keyboard_v1_removed,
    .input_device = river_xkb_keyboard_v1_input_device,
    .layout = river_xkb_keyboard_v1_layout,
    .capslock_enabled = river_xkb_keyboard_v1_capslock_enabled,
    .capslock_disabled = river_xkb_keyboard_v1_capslock_disabled,
    .numlock_enabled = river_xkb_keyboard_v1_numlock_enabled,
    .numlock_disabled = river_xkb_keyboard_v1_numlock_disabled,
};

void river_xkb_config_v1_finished(
    void* data, struct river_xkb_config_v1* river_xkb_config_v1) {
    river_xkb_config_v1_destroy(river_xkb_config_v1);
}

void river_xkb_config_v1_xkb_keyboard(
    void* data, struct river_xkb_config_v1* river_xkb_config_v1, struct river_xkb_keyboard_v1* id) {
    Keyboard* keyboard = calloc(1, sizeof(Keyboard));
    keyboard->river_xkb_keyboard = id;

    wl_list_insert(&anvl.keyboards, &keyboard->link);
    river_xkb_keyboard_v1_add_listener(id, &xkb_keyboard_listener, keyboard);

    if (xkb_keymap) {
        river_xkb_keyboard_v1_set_keymap(keyboard->river_xkb_keyboard, xkb_keymap);
    }
}

const struct river_xkb_config_v1_listener xkb_config_listener = {
    .finished = river_xkb_config_v1_finished,
    .xkb_keyboard = river_xkb_config_v1_xkb_keyboard,
};

struct river_xkb_keymap_v1* create_keymap() {
    struct xkb_rule_names keymap_rule_names = { 0 };
    keymap_rule_names.layout = kb_layout;

    struct xkb_keymap* keymap = xkb_keymap_new_from_names2(
        xkb_context, &keymap_rule_names, XKB_KEYMAP_FORMAT_TEXT_V2, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "Failed to create xkb keymap\n");
        return NULL;
    }

    char* keymap_str = xkb_keymap_get_as_string2(
        keymap, XKB_KEYMAP_FORMAT_TEXT_V2, XKB_KEYMAP_SERIALIZE_NO_FLAGS);
    xkb_keymap_unref(keymap);
    int keymap_str_len = strlen(keymap_str) + 1;
    int keymap_fd = memfd_create("anvl-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (keymap_fd == -1 || ftruncate(keymap_fd, keymap_str_len) < 0) {
        fprintf(stderr, "Failed to create or truncate mem fd\n");
        close(keymap_fd);
        free(keymap_str);
        return NULL;
    }

    void* data = mmap(NULL, keymap_str_len, PROT_READ | PROT_WRITE, MAP_SHARED, keymap_fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "Failed to map data\n");
        close(keymap_fd);
        free(keymap_str);
        return NULL;
    }

    memcpy(data, keymap_str, keymap_str_len);
    free(keymap_str);

    if (munmap(data, keymap_str_len) < 0) {
        fprintf(stderr, "Failed to unmap data\n");
        close(keymap_fd);
        free(keymap_str);
        return NULL;
    }

    if (fcntl(keymap_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        fprintf(stderr, "Failed to seal mem fd\n");
        close(keymap_fd);
        free(keymap_str);
        return NULL;
    }

    return river_xkb_config_v1_create_keymap(xkb_config, keymap_fd, XKB_KEYMAP_FORMAT_TEXT_V2);
}

void river_xkb_keymap_v1_success(
    void* data, struct river_xkb_keymap_v1* river_xkb_keymap_v1) {
    Keyboard* keyboard;
    wl_list_for_each(keyboard, &anvl.keyboards, link) {
        river_xkb_keyboard_v1_set_keymap(keyboard->river_xkb_keyboard, xkb_keymap);
    }
}

void river_xkb_keymap_v1_failure(
    void* data, struct river_xkb_keymap_v1* river_xkb_keymap_v1, const char* error_msg) {
    fprintf(stderr, "Failed to create keymap\n");
}

const struct river_xkb_keymap_v1_listener xkb_keymap_listener = {
    .success = river_xkb_keymap_v1_success,
    .failure = river_xkb_keymap_v1_failure,
};

void wl_output_geometry(void* data, struct wl_output* wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char* make, const char* model, int32_t transform) { }

void wl_output_mode(void* data, struct wl_output* wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0)
        return;

    WlOutput* output = data;
    output->width = width;
    output->height = height;
}

void wl_output_done(void* data, struct wl_output* wl_output) {
    WlOutput* output = data;

    output->surface = wl_compositor_create_surface(compositor);

    struct wl_region* input_region = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(output->surface, input_region);
    wl_region_destroy(input_region);

    // Layer set to 1 so fullscreen windows will render above it
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        zwlr_layer_shell, output->surface, output->wl_output, 1, "bar");

    /* Width is zero because the left and right anchors make the compositor pick
     * the output's logical width. wl_output.mode reports physical pixels, which
     * made the bar too wide on scaled outputs. */
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, barpx);
    zwlr_layer_surface_v1_set_anchor(
        output->layer_surface, (top_bar ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);

    zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);
    wl_surface_commit(output->surface);

    struct wl_callback* cb = wl_surface_frame(output->surface);
    wl_callback_add_listener(cb, &wl_surface_frame_listener, output);
}

void wl_output_scale(void* data, struct wl_output* wl_output, int32_t factor) {
    WlOutput* output = data;
    output->scale = MAX(factor, 1);
}
void wl_output_name(void* data, struct wl_output* wl_output, const char* name) {
}
void wl_output_description(void* data, struct wl_output* wl_output, const char* description) { }

const struct wl_output_listener wl_output_listener = {
    .geometry = wl_output_geometry,
    .mode = wl_output_mode,
    .done = wl_output_done,
    .scale = wl_output_scale,
    .name = wl_output_name,
    .description = wl_output_description,
};

void wl_registry_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (strcmp(interface, river_window_manager_v1_interface.name) == 0) {
        window_manager = wl_registry_bind(registry, name, &river_window_manager_v1_interface, 4);
        if (window_manager) {
            river_window_manager_v1_add_listener(window_manager, &window_manager_listener, NULL);
        }
    }

    if (strcmp(interface, river_xkb_bindings_v1_interface.name) == 0) {
        xkb_bindings = wl_registry_bind(registry, name, &river_xkb_bindings_v1_interface, 1);
    }

    if (strcmp(interface, river_input_manager_v1_interface.name) == 0) {
        input_manager = wl_registry_bind(registry, name, &river_input_manager_v1_interface, 1);
        if (input_manager != NULL) {
            river_input_manager_v1_add_listener(input_manager, &input_manager_listener, NULL);
        }
    }

    if (strcmp(interface, river_xkb_config_v1_interface.name) == 0) {
        xkb_config = wl_registry_bind(registry, name, &river_xkb_config_v1_interface, 1);
        if (xkb_config) {
            river_xkb_config_v1_add_listener(xkb_config, &xkb_config_listener, NULL);

            xkb_keymap = create_keymap();
            if (xkb_keymap) {
                river_xkb_keymap_v1_add_listener(xkb_keymap, &xkb_keymap_listener, NULL);
            }
        }
    }

    if (strcmp(interface, wl_output_interface.name) == 0) {
        WlOutput* output = calloc(1, sizeof(WlOutput));
        output->scale = 1;
        output->done = false;
        output->configured = false;
        output->name = name;
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(output->wl_output, &wl_output_listener, output);
        wl_list_insert(&anvl.wl_outputs, &output->link);
    }

    if (strcmp(interface, river_layer_shell_v1_interface.name) == 0) {
        layer_shell = wl_registry_bind(registry, name, &river_layer_shell_v1_interface, 1);
    }

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        zwlr_layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    }

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
}

void wl_registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) { }

const struct wl_registry_listener registry_listener = {
    .global = wl_registry_global,
    .global_remove = wl_registry_global_remove,
};
