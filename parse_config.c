#include "libevdev/libevdev.h"
#include "synthetic-mouse.h"
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOCAL_CONF_NAME "synthetic.conf"

#define UP "UP="
#define DOWN "DOWN="
#define LEFT "LEFT="
#define RIGHT "RIGHT="
#define RIGHT_CLICK "RIGHT_CLICK="
#define LEFT_CLICK "LEFT_CLICK="
#define SCROLL_DOWN "SCROLL_DOWN="
#define SCROLL_UP "SCROLL_UP="
#define SCROLL_CLICK "SCROLL_CLICK="
#define MOUSE_BREAK "MOUSE_BREAK="

#define ACCELERATION "ACCELERATION="
#define BREAK_FACTOR "BREAK_FACTOR="
#define MAX_SPEED "MAX_SPEED="

#define PHYS_NAME "PHYS_NAME="

ptrdiff_t strip_fluff(char *str) {
    char *read = str, *write = str;

    while (*read != '\0') {
        if (isspace(*read)) {
            ++read;
            continue;
        }

        if (*read == '#') {
            break;
        }

        *write = toupper(*read);

        ++write;
        ++read;
    }

    *write = '\0';
    return write - str;
}

int is_hotkey(const char *line, struct conf_data *data) {
    int len = 0;

    if (strncmp(line, UP, (len = strlen(UP))) == 0) {
        data->up = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, DOWN, (len = strlen(DOWN))) == 0) {
        data->down = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, LEFT, (len = strlen(LEFT))) == 0) {
        data->left = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, RIGHT, (len = strlen(RIGHT))) == 0) {
        data->right = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, RIGHT_CLICK, (len = strlen(RIGHT_CLICK))) == 0) {
        data->right_click = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, LEFT_CLICK, (len = strlen(LEFT_CLICK))) == 0) {
        data->left_click = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, SCROLL_DOWN, (len = strlen(SCROLL_DOWN))) == 0) {
        data->scroll_down = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, SCROLL_UP, (len = strlen(SCROLL_UP))) == 0) {
        data->scroll_up = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, SCROLL_CLICK, (len = strlen(SCROLL_CLICK))) == 0) {
        data->scroll_click = libevdev_event_code_from_code_name(line + len);
        return 1;
    }
    if (strncmp(line, MOUSE_BREAK, (len = strlen(MOUSE_BREAK))) == 0) {
        data->mouse_break = libevdev_event_code_from_code_name(line + len);
        return 1;
    }

    return 0;
}

int is_var(const char *line, struct conf_data *data) {
    int len;

    if (strncmp(line, ACCELERATION, (len = strlen(ACCELERATION))) == 0) {
        char *endptr = 0;
        data->acceleration = strtof(line + len, &endptr);
        return endptr != line + len;
    }

    if (strncmp(line, BREAK_FACTOR, (len = strlen(BREAK_FACTOR))) == 0) {
        char *endptr = 0;
        data->break_factor = strtof(line + len, &endptr);
        return endptr != line + len;
    }

    if (strncmp(line, MAX_SPEED, (len = strlen(MAX_SPEED))) == 0) {
        char *endptr = 0;
        data->max_speed = strtof(line + len, &endptr);
        return endptr != line + len;
    }

    return 0;
}

struct conf_data parse_config() {
    struct conf_data data = {0};
    size_t size = 16;
    char *buf = malloc(size);
    ptrdiff_t line;
    FILE *conf_file = fopen(LOCAL_CONF_NAME, "r");

    if (conf_file == 0) {
        fprintf(stderr, "Error opening config file %s\n", LOCAL_CONF_NAME);
        exit(1);
    }

    while (getline(&buf, &size, conf_file) != EOF) {
        line = strip_fluff(buf);
        if (line == 0)
            continue;

        if (is_hotkey(buf, &data))
            continue;

        if (is_var(buf, &data))
            continue;

        int len;
        if (strncmp(buf, PHYS_NAME, (len = strlen(PHYS_NAME))) == 0) {
            for (char *c = buf + len; *c != '\0'; ++c)
                *c = tolower(*c);
            data.phys_name = strdup(buf + len);
        }
    }

    free(buf);
    fclose(conf_file);

    // TODO: Check if data is filled
    printf("up: %s\n"
           "down: %s\n"
           "left: %s\n"
           "right: %s\n"
           "right_click: %s\n"
           "left_click: %s\n"
           "scroll_down: %s\n"
           "scroll_up: %s\n"
           "scroll_click: %s\n"
           "mouse_break: %d\n"
           "phys_name: %s\n"
           "acceleration: %f\n"
           "break_factor: %f\n"
           "max_speed: %f\n",
           libevdev_event_code_get_name(EV_KEY, data.up),
           libevdev_event_code_get_name(EV_KEY, data.down),
           libevdev_event_code_get_name(EV_KEY, data.left),
           libevdev_event_code_get_name(EV_KEY, data.right),
           libevdev_event_code_get_name(EV_KEY, data.right_click),
           libevdev_event_code_get_name(EV_KEY, data.left_click),
           libevdev_event_code_get_name(EV_KEY, data.scroll_down),
           libevdev_event_code_get_name(EV_KEY, data.scroll_up),
           libevdev_event_code_get_name(EV_KEY, data.scroll_click),
           data.mouse_break, data.phys_name ? data.phys_name : "",
           data.acceleration, data.break_factor, data.max_speed);

    return data;
}
