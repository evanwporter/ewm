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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fcft/fcft.h>

#include "anvl.h"
#include "config.h"
#include "river.h"

#define MAX(A, B) (A > B ? A : B)
#define LENGTH(A) (sizeof A / sizeof A[0])

struct fcft_font* fcft_font = NULL;
static int fcft_font_scale = 0;
bool set_font_scale(int scale) {
    if (scale == fcft_font_scale)
        return true;

    char attributes[32];
    snprintf(attributes, sizeof(attributes), "pixelsize=%d", fontpx * scale);
    const char* names[] = { font };
    struct fcft_font* scaled_font = fcft_from_name2(1, names, attributes, NULL);
    if (scaled_font == NULL) {
        fprintf(stderr, "font failed at scale %d\n", scale);
        return false;
    }

    if (fcft_font != NULL)
        fcft_destroy(fcft_font);
    fcft_font = scaled_font;
    fcft_font_scale = scale;
    return true;
}

static bool read_line(const char* path, char* buf, size_t size) {
    FILE* file = fopen(path, "r");
    if (file == NULL)
        return false;

    bool ok = fgets(buf, size, file) != NULL;
    fclose(file);
    if (ok)
        buf[strcspn(buf, "\n")] = '\0';
    return ok;
}

static void wifi_status(char* buf, size_t size) {
    DIR* net = opendir("/sys/class/net");
    if (net == NULL) {
        snprintf(buf, size, "Wi-Fi: ?");
        return;
    }

    bool found = false;
    bool connected = false;
    struct dirent* entry;
    while ((entry = readdir(net)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char path[PATH_MAX];
        struct stat st;
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", entry->d_name);
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        found = true;
        snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", entry->d_name);
        char state[16];
        if (read_line(path, state, sizeof(state)) && strcmp(state, "up") == 0) {
            connected = true;
            break;
        }
    }
    closedir(net);

    snprintf(buf, size, "Wi-Fi: %s", !found ? "n/a" : connected ? "on"
                                                                : "off");
}

static void battery_status(char* buf, size_t size) {
    DIR* power = opendir("/sys/class/power_supply");
    if (power == NULL) {
        snprintf(buf, size, "Bat: n/a");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(power)) != NULL) {
        if (strncmp(entry->d_name, "BAT", 3) != 0)
            continue;

        char path[PATH_MAX], capacity[16];
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", entry->d_name);
        if (read_line(path, capacity, sizeof(capacity))) {
            snprintf(buf, size, "Bat: %d%%", atoi(capacity));
            closedir(power);
            return;
        }
    }
    closedir(power);
    snprintf(buf, size, "Bat: n/a");
}

static void format_status(char* buf, size_t size) {
    char wifi[24], battery[24], clock[64];
    wifi_status(wifi, sizeof(wifi));
    battery_status(battery, sizeof(battery));

    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time) == NULL)
        snprintf(clock, sizeof(clock), "time: ?");
    else
        strftime(clock, sizeof(clock), status_time_format, &local_time);

    snprintf(buf, size, "%s  |  %s  |  %s", wifi, battery, clock);
}

void randname(char* buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0)
        return -1;

    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
static pixman_color_t normfg = { 0xebeb, 0xdbdb, 0xb2b2, 0xffff };
static pixman_color_t normbg = { 0x2828, 0x2828, 0x2828, 0xffff };
static pixman_color_t selfg = { 0xfbfb, 0xf1f1, 0xc7c7, 0xffff };
static pixman_color_t selbg = { 0xe7e7, 0x8a8a, 0x3e3e, 0xffff };

int lx(int x, int width, int text_width) { return x; }
int cx(int x, int width, int text_width) {
    return x + (width - text_width) / 2;
}

int rx(int x, int width, int text_width) { return x - text_width; }
void render_chars(const char* chars, size_t len, int x, int y, int width, int (*fx)(int, int, int), pixman_image_t* pix, pixman_image_t* color) {
    const struct fcft_glyph* glyphs[len];
    long kern[len];
    int text_width = 0;

    for (size_t i = 0; i < len; i++) {
        glyphs[i] = fcft_rasterize_char_utf32(fcft_font, chars[i], FCFT_SUBPIXEL_NONE);
        if (glyphs[i] == NULL)
            continue;

        kern[i] = 0;
        if (i > 0) {
            long x_kern;
            if (fcft_kerning(fcft_font, chars[i - 1], chars[i], &x_kern, NULL))
                kern[i] = x_kern;
        }

        text_width += kern[i] + glyphs[i]->advance.x;
    }

    int cx = fx(x, width, text_width);
    for (size_t i = 0; i < len; i++) {
        const struct fcft_glyph* g = glyphs[i];
        if (g == NULL)
            continue;
        cx += kern[i];

        pixman_image_composite32(PIXMAN_OP_OVER, color, g->pix, pix, 0, 0, 0, 0, cx + g->x, y + fcft_font->ascent - g->y, g->width, g->height);

        cx += g->advance.x;
    }
}

int text_width(const char* text) {
    int width = 0;

    for (size_t i = 0; text[i] != '\0'; i++) {
        const struct fcft_glyph* glyph = fcft_rasterize_char_utf32(fcft_font, text[i], FCFT_SUBPIXEL_NONE);
        if (glyph != NULL)
            width += glyph->advance.x;
    }

    return width;
}

void render_bar(WlOutput* output) {
    /* A layer surface must receive and acknowledge its first configure before
     * it is committed.  Output discovery can arrive in either order. */
    if (!output->configured)
        return;

    if (!show_bar) {
        wl_surface_commit(output->surface);
        return;
    }

    if (!output->done)
        return;
    int scale = output->scale;
    if (!set_font_scale(scale))
        return;

    /* Layer-shell configure dimensions are logical.  Back the surface with a
     * native-resolution buffer so the compositor does not upscale bar text. */
    int w = output->width * scale, h = barpx * scale;

    uint32_t stride = w * 4;
    int shm_pool_size = h * stride;

    int fd = allocate_shm_file(shm_pool_size);
    uint8_t* pool_data = mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct wl_shm_pool* pool = wl_shm_create_pool(river_shm(), fd, shm_pool_size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    pixman_image_t* pix = NULL;
    pix = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, w, h, (void*)pool_data, stride);
    if (pix == NULL) {
        fprintf(stderr, "error: failed to create pixman image\n");
        return;
    }

    pixman_image_fill_rectangles(PIXMAN_OP_SRC, pix, &normbg, 1, (pixman_rectangle16_t[]) { { 0, 0, w, h } });

    pixman_image_t* fg = pixman_image_create_solid_fill(&normfg);
    pixman_image_t* sel_fg = pixman_image_create_solid_fill(&selfg);
    int y = (h - fcft_font->height) / 2;

    /// Holds the x position within the bar window.
    int x = 0;

    /// Holds temporary width values when drawing the bar.
    int textw;

    /// Common iterator.
    unsigned int i;

    /// Bitmask that holds occupied workspaces.
    unsigned int occ = 0;

    Window* window;
    wl_list_for_each(window, &anvl.windows, link) {
        if (window->node != NULL)
            occ |= 1U << window->node->tag->n;
    }

    /* We start by looping through all tags. Do not draw vacant tags, except for
     * the selected one. This is the same rule as dwm's drawbar(). */
    for (i = 0; i < LENGTH(tags); i++) {
        if (!(occ & (1U << i) || i == output->output->seltag))
            continue;

        textw = text_width(tags[i]) + h;
        bool selected = i == output->output->seltag;
        if (selected)
            pixman_image_fill_rectangles(PIXMAN_OP_SRC, pix, &selbg, 1, (pixman_rectangle16_t[]) { { x, 0, textw, h } });
        render_chars(tags[i], strlen(tags[i]), x, y, textw, &cx, pix, selected ? sel_fg : fg);
        x += textw;
    }

    /* Just draw the layout symbol. */
    const char* symbol = output->output->tags[output->output->seltag]->lt->symbol;
    textw = text_width(symbol) + h;
    render_chars(symbol, strlen(symbol), x, y, textw, &cx, pix, fg);
    x += textw;

    /* Draw window titles. The window-management protocol supplies titles but
     * not dwm's icon or scratchpad metadata, so tabs are the direct equivalent
     * of the title portion of dwm's bar. */
    int n = 0;
    wl_list_for_each(window, &anvl.windows, link) {
        if (window->node != NULL && window->node->tag == output->output->tags[output->output->seltag])
            n++;
    }

    char status[128];
    format_status(status, sizeof(status));
    int status_len = strlen(status);
    int statusw = text_width(status) + h;

    /* Draw status first so it can be overdrawn by tags later. This follows
     * dwm's drawbar() ordering and reserves its rightmost space for the status.
     */
    if (output->output == selmon)
        render_chars(status, status_len, w - h / 2, y, 0, &rx, pix, fg);
    else
        statusw = 0;

    if ((textw = w - statusw - x) > h && n > 0) {
        int remainder = textw % n;
        int tabw = textw / n;
        wl_list_for_each(window, &anvl.windows, link) {
            if (window->node == NULL || window->node->tag != output->output->tags[output->output->seltag])
                continue;

            int tab_width = tabw + (remainder-- > 0 ? 1 : 0);
            bool selected = false;
            Seat* seat;
            wl_list_for_each(seat, &anvl.seats, link) {
                if (seat->focused == window) {
                    selected = true;
                    break;
                }
            }
            if (selected)
                pixman_image_fill_rectangles(
                    PIXMAN_OP_SRC, pix, &selbg, 1, (pixman_rectangle16_t[]) { { x, 0, tab_width, h } });
            if (window->title != NULL)
                render_chars(window->title, strlen(window->title), x + h / 2, y, tab_width - h, &lx, pix, selected ? sel_fg : fg);
            x += tab_width;
        }
    }

    pixman_image_unref(sel_fg);
    pixman_image_unref(fg);

    wl_surface_attach(output->surface, buf, 0, 0);
    wl_surface_set_buffer_scale(output->surface, scale);
    wl_surface_damage(output->surface, 0, 0, output->width, barpx);
    wl_surface_commit(output->surface);

    wl_shm_pool_destroy(pool);
    pool = NULL;
    close(fd);
    fd = -1;

    pixman_image_unref(pix);
    wl_buffer_destroy(buf);
    munmap(pool_data, shm_pool_size);
}
