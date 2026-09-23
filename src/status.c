#include <linux/limits.h>
#include <sys/mman.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fcft/fcft.h>

#include "config.h"

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

void format_status(char* buf, size_t size) {
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
