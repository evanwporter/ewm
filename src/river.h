#ifndef RIVERH
#define RIVERH

#include "anvl.h"

#include <river-input-management-v1-client-protocol.h>
#include <river-layer-shell-v1-client-protocol.h>
#include <river-window-management-v1-client-protocol.h>
#include <river-xkb-bindings-v1-client-protocol.h>
#include <river-xkb-config-v1-client-protocol.h>
#include <wlr-layer-shell-unstable-v1-client-protocol.h>

bool river_init(void);
bool river_supported(void);

void river_window_close(Client* window);
void river_window_show(Client* window);
void river_window_hide(Client* window);
void river_window_move(Client* window, int x, int y);
void river_window_resize(Client* window, int width, int height);
void river_window_prepare(Client* window);
void river_window_set_borders(Client* window, bool focused);

void river_focus_window(Seat* seat, Client* window);
void river_clear_focus(Seat* seat);
void river_raise_window(Client* window);
void river_pointer_warp(Seat* seat, int x, int y);
void river_select_output(Output* output);
void river_exit_session(void);

struct wl_shm* river_shm(void);
extern const struct wl_registry_listener registry_listener;

#endif
