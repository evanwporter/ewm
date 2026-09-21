#include "anvl.h"
#include <river-window-management-v1-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static const unsigned int gappx = 4; // 0 to disable gap
static const float default_master_ratio = 0.55;
static const unsigned int default_master_count = 1;
static const bool lock_fullscreen = true;
static const bool swallow_floating = false;
/*
 * Rules match substrings in Wayland's app_id and window title. A NULL filter
 * does not participate in matching, and the last matching rule wins.
 *
 * app_id is normally the application ID from the client's .desktop file.
 * Unlike X11, Wayland has no separate class and instance.
 */
static const Rule rules[] = {
    { .app_id = "kitty", .isterminal = 1 },
    { .app_id = "foot", .isterminal = 1 },
    { .app_id = "st-256color", .isterminal = 1 },
};
/* The focused tiled client gets this compositor-drawn border when it has company. */
static const int selected_border_width = 3;
static const uint32_t selected_border_r = 0xe7e7e7e7U;
static const uint32_t selected_border_g = 0x8a8a8a8aU;
static const uint32_t selected_border_b = 0x3e3e3e3eU;
static const bool show_bar = true;
static const int barpx = 24;
static const bool top_bar = false; // 0 means bottom bar
static const char* font = "JetBrainsMonoNL NFP:style=Regular";
static const int fontpx = 13;
static const char* status_time_format = "%a, %d %b %H:%M";
static const char* kb_layout = "us";

static const char* workspace_names[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static Layout layouts[] = {
    { "[]=", tile }, // First entry is default
    { "[M]", monocle },
};

#define CONTROL RIVER_SEAT_V1_MODIFIERS_CTRL
#define SUPER RIVER_SEAT_V1_MODIFIERS_MOD4
#define SHIFT RIVER_SEAT_V1_MODIFIERS_SHIFT
#define ALT RIVER_SEAT_V1_MODIFIERS_MOD1

#define WORKSPACEKEY(KEY, WORKSPACE) \
    { SUPER, KEY, viewworkspace, { .u = WORKSPACE + 1 } }, { SUPER | SHIFT, KEY, movetoworkspace, { .u = WORKSPACE + 1 } },

static const char* termcmd[] = { "kitty", NULL };
static const char* reloadcmd[] = { "sh", "-c", "pkill -x anvl; exec anvl", NULL };
static const char* menucmd[] = {
    "bemenu-run",
    "-l",
    "10",
    "-p",
    "Run:",
    "--fn",
    "JetBrainsMonoNL NFP 13",
    NULL
};

static Keys keybinds[] = {
    { SUPER, XKB_KEY_Return, spawn, { .v = termcmd } },
    { SUPER | SHIFT, XKB_KEY_c, spawn, { .v = reloadcmd } },
    { SUPER, XKB_KEY_space, spawn, { .v = menucmd } },
    { SUPER, XKB_KEY_t, setlayout, { .v = &layouts[0] } },
    { SUPER, XKB_KEY_m, setlayout, { .v = &layouts[1] } },
    { SUPER, XKB_KEY_h, setmfact, { .f = -0.05 } },
    { SUPER, XKB_KEY_l, setmfact, { .f = 0.05 } },
    { SUPER, XKB_KEY_i, incnmaster, { .i = +1 } },
    { SUPER, XKB_KEY_d, incnmaster, { .i = -1 } },
    { SUPER, XKB_KEY_q, destroy_window, { 0 } },
    { SUPER | SHIFT, XKB_KEY_e, exit_session, { 0 } },
    { SUPER, XKB_KEY_j, focus_next, { 0 } },
    { SUPER, XKB_KEY_k, focus_prev, { 0 } },
    { SUPER, XKB_KEY_period, focus_next_mon, { 0 } },
    { SUPER, XKB_KEY_comma, focus_prev_mon, { 0 } },
    { SUPER | SHIFT, XKB_KEY_period, tag_next_mon, { 0 } },
    { SUPER | SHIFT, XKB_KEY_comma, tag_prev_mon, { 0 } },
    WORKSPACEKEY(XKB_KEY_1, 0) WORKSPACEKEY(XKB_KEY_2, 1) WORKSPACEKEY(XKB_KEY_3, 2)
        WORKSPACEKEY(XKB_KEY_4, 3) WORKSPACEKEY(XKB_KEY_5, 4) WORKSPACEKEY(XKB_KEY_6, 5)
            WORKSPACEKEY(XKB_KEY_7, 6) WORKSPACEKEY(XKB_KEY_8, 7) WORKSPACEKEY(XKB_KEY_9, 8)
};
