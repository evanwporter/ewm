#include "anvl.h"
#include <river-window-management-v1-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static const unsigned int gappx = 4; // 0 to disable gap
static const float default_master_ratio = 0.55;
static const unsigned int default_master_count = 1;
static const bool lock_fullscreen = true;
static const bool show_bar = true;
static const int barpx = 24;
static const bool top_bar = false; // 0 means bottom bar
static const char* font = "JetBrainsMonoNL NFP:style=Regular";
static const int fontpx = 13;
static const char* status_time_format = "%a, %d %b %H:%M";
static const char* kb_layout = "us";

static const char* tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static Layout layouts[] = {
    { "[]=", tile }, // First entry is default
    { "[M]", monocle },
};

#define CONTROL RIVER_SEAT_V1_MODIFIERS_CTRL
#define SUPER RIVER_SEAT_V1_MODIFIERS_MOD4
#define SHIFT RIVER_SEAT_V1_MODIFIERS_SHIFT
#define ALT RIVER_SEAT_V1_MODIFIERS_MOD1

#define TAGKEY(KEY, TAG) \
    { SUPER, KEY, view, { .u = TAG } }, { SUPER | SHIFT, KEY, tag, { .u = TAG } },

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
    { SUPER, XKB_KEY_t, set_layout, { .v = &layouts[0] } },
    { SUPER, XKB_KEY_m, set_layout, { .v = &layouts[1] } },
    { SUPER, XKB_KEY_h, set_master_ratio, { .f = -0.05 } },
    { SUPER, XKB_KEY_l, set_master_ratio, { .f = 0.05 } },
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
    TAGKEY(XKB_KEY_1, 0) TAGKEY(XKB_KEY_2, 1) TAGKEY(XKB_KEY_3, 2)
        TAGKEY(XKB_KEY_4, 3) TAGKEY(XKB_KEY_5, 4) TAGKEY(XKB_KEY_6, 5)
            TAGKEY(XKB_KEY_7, 6) TAGKEY(XKB_KEY_8, 7) TAGKEY(XKB_KEY_9, 8)
};
