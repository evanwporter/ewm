#include "anvl.h"
#include "config.h"
#include "river.h"

#define MIN(A, B) (A < B ? A : B)

static bool isvisible(const Client* client, const Output* output) {
    return client->mon == output && client->workspace == output->seltag;
}

/// This handles the DWM-style master/stack tile arrangement.
void tile(Output* output) {
    /* Variables:
     *    i - iterator, represents number of clients processed
     *    n - total number of visible clients
     *    h - calculated client height
     *    mw - calculated width of the master area
     *    my - calculated master-area y position relative to the window area
     *    ty - calculated stack-area y position relative to the window area
     */
    unsigned int i, n, h, mw, my, ty;
    Workspace* workspace = output->tags[output->seltag];
    Client* client;

    // Sets n to the number of visible clients on this output and workspace.
    for (n = 0, client = output->clients; client; client = client->next)
        if (isvisible(client, output))
            n++;

    if (n == 0)
        return;

    if (n > workspace->master_count)
        mw = workspace->master_count ? output->ww * workspace->master_ratio : 0;
    else
        mw = output->ww;

    for (i = my = ty = 0, client = output->clients; client; client = client->next) {
        if (!isvisible(client, output))
            continue;

        if (i < workspace->master_count) {
            h = (output->wh - my - gappx * (MIN(n, workspace->master_count) - i - 1))
                / (MIN(n, workspace->master_count) - i);
            river_window_show(client);
            river_window_move(client, output->wx, output->wy + my);
            river_window_resize(client, mw, h);
            my += h + (i + 1 < MIN(n, workspace->master_count) ? gappx : 0);
        } else {
            h = (output->wh - ty - gappx * (n - i - 1)) / (n - i);
            river_window_show(client);
            river_window_move(client, output->wx + mw + (workspace->master_count ? gappx : 0), output->wy + ty);
            river_window_resize(client, output->ww - mw - (workspace->master_count ? gappx : 0), h);
            ty += h + (i + 1 < n ? gappx : 0);
        }
        i++;
    }
}
