#include "anvl.h"
#include "config.h"
#include "river.h"

#define MIN(A, B) (A < B ? A : B)

/// This is what handles the tile layout arrangement.
void tile(Output* output) {
    /* Variables:
     *    i - iterator, represents number of clients processed
     *    count - total number of tiled clients
     *    height - calculated client height
     *    master_width - calculated width of the master area
     *    master_y - calculated master-area y position relative to the window area
     *    stack_y - calculated stack-area y position relative to the window area
     *
     * EWM stores clients in a workspace tree rather than DWM's linked client
     * list. Walking the leaves yields the tiled clients in insertion order.
     */
    Node* queue[1 << 16];
    Client* clients[1 << 16];
    uint32_t front = 0, back = 0, count = 0;
    Workspace* workspace = output->tags[output->seltag];

    queue[back++] = workspace->root;
    while (front != back) {
        Node* node = queue[front++];
        if (node->first != NULL && node->second != NULL) {
            queue[back++] = node->first;
            queue[back++] = node->second;
        } else if (node->window != NULL) {
            clients[count++] = node->window;
        }
    }

    if (count == 0)
        return;

    int x = workspace->root->x;
    int y = workspace->root->y;
    int width = workspace->root->width;
    int height = workspace->root->height;
    uint32_t master_count = MIN(count, workspace->master_count);
    int master_width = count > workspace->master_count
        ? (workspace->master_count ? (int)((width - gappx) * workspace->master_ratio) : 0)
        : width;
    int stack_width = width - master_width - (count > workspace->master_count ? gappx : 0);
    int master_y = 0;
    int stack_y = 0;

    for (uint32_t i = 0; i < count; i++) {
        Client* client = clients[i];
        int client_x;
        int client_y;
        int client_width;
        int client_height;

        if (i < master_count) {
            client_height = (height - master_y - gappx * (master_count - i - 1))
                / (master_count - i);
            client_x = x;
            client_y = y + master_y;
            client_width = master_width;
            master_y += client_height + (i + 1 < master_count ? gappx : 0);
        } else {
            uint32_t stack_remaining = count - i;
            client_height = (height - stack_y - gappx * (stack_remaining - 1)) / stack_remaining;
            client_x = x + master_width + (workspace->master_count ? gappx : 0);
            client_y = y + stack_y;
            client_width = stack_width;
            stack_y += client_height + (i + 1 < count ? gappx : 0);
        }

        river_window_show(client);
        river_window_resize(client, client_width, client_height);
        river_window_move(client, client_x, client_y);
    }
}
