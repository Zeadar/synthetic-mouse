#include "libevdev/libevdev.h"
#include "synthetic-mouse.h"
#include <ctype.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEV_ID "dev_id"
#define PASSTHROUGH '!'
#define LOCAL_CONF_NAME "synthetic.conf"
#define XDG_CONF_SUBPATH "synthetic-mouse/synthetic.conf"
#define SYSTEM_CONF_NAME "/etc/synthetic-mouse/synthetic.conf"
#define ENABLE_PASSTHROUGH "enable_passthrough"

extern int is_quiet; // main.c

enum READMODE {
    PROPERTY,
    KEY_VALUE,
    VAR_VALUE,
    DEV_VALUE,
    RANGE,
    SKIPLINE,
    WHITESPACE,
    READ,
};

static const char *key_names[KEY_ID_COUNT] = {
#define GENERATE_KEY_NAME(_, KEY_NAME_LOWER, __) #KEY_NAME_LOWER,
    X_FOR_EACH_KEY(GENERATE_KEY_NAME)
#undef GENERATE_KEY_NAME
};

static const char *var_names[VAR_ID_COUNT] = {
#define GENERATE_VAR_NAME(_, VAR_NAME_LOWER) #VAR_NAME_LOWER,
    X_FOR_EACH_VAR(GENERATE_VAR_NAME)
#undef GENERATE_VAR_NAME
};

#define STARTSIZE 16
char *unibuf(int c) {
    static size_t size = STARTSIZE;
    static char *buf = 0;
    static char *write = 0;

    if (buf == 0) {
        buf = malloc(size);
        memset(buf, 0, size);
        write = buf;
    }

    if (c == EOF) {
        free(buf);
        buf = write = 0;
        size = STARTSIZE;
        return 0;
    }

    *write++ = (char) c;

    if (size == (size_t) (write - buf)) {
        buf = realloc(buf, size * 2);
        memset(buf + size, 0, size);
        write = buf + size;
        size *= 2;
    }

    return buf;
}
#undef STARTSIZE

int is_key(char *buf, enum KEY_ID *key_id) {
    for (int i = 0; i != KEY_ID_COUNT; ++i) {
        if (strcmp(buf, key_names[i]) == 0) {
            *key_id = i;
            return 1;
        }
    }
    return 0;
}

int is_var(char *buf, enum VAR_ID *var_id) {
    for (int i = 0; i != VAR_ID_COUNT; ++i) {
        if (strcmp(buf, var_names[i]) == 0) {
            *var_id = i;
            return 1;
        }
    }
    return 0;
}

int is_range(char *buf, int *release, int *press) {
    char *separator = strstr(buf, "..");
    char *endptr;

    if (separator == 0)
        return 0;

    *separator = '\0';

    long release_value = strtol(buf, &endptr, 10);
    if (endptr == buf || *endptr != '\0') {
        *separator = '.';
        return 0;
    }

    char *press_str = separator + 2;
    long press_value = strtol(press_str, &endptr, 10);
    if (endptr == press_str || *endptr != '\0') {
        *separator = '.';
        return 0;
    }

    *separator = '.';
    *release = (int) release_value;
    *press = (int) press_value;
    return 1;
}

const char *key_repr(struct key *key) {
    if (key->ev_code == 0 && key->ev_type == 0)
        return "(Unassigned)";
    return libevdev_event_code_get_name(key->ev_type, key->ev_code);
}

static struct key *conf_key_slot(struct conf_data *data, enum KEY_ID key_id) {
    int key_index = key_id;

    if (key_index < HOLDABLE_ID_COUNT)
        return &data->hold_keys[key_index];

    if (key_index < HOLDABLE_ID_COUNT + CLICKABLE_ID_COUNT)
        return &data->click_keys[key_index - HOLDABLE_ID_COUNT];

    return &data->func_keys[key_index - HOLDABLE_ID_COUNT - CLICKABLE_ID_COUNT];
}

static FILE *open_config_file(char *resolved_path, size_t resolved_path_size) {
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char xdg_conf_path[PATH_MAX];
    const char *candidates[] = {
        LOCAL_CONF_NAME,
        0,
        SYSTEM_CONF_NAME,
    };

    if (xdg_config_home != 0 && xdg_config_home[0] != '\0') {
        snprintf(xdg_conf_path, sizeof(xdg_conf_path), "%s/%s", xdg_config_home,
                 XDG_CONF_SUBPATH);
        candidates[1] = xdg_conf_path;
    } else if (home != 0 && home[0] != '\0') {
        snprintf(xdg_conf_path, sizeof(xdg_conf_path), "%s/.config/%s", home,
                 XDG_CONF_SUBPATH);
        candidates[1] = xdg_conf_path;
    }

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (candidates[i] == 0)
            continue;

        FILE *conf_file = fopen(candidates[i], "r");
        if (conf_file != 0) {
            snprintf(resolved_path, resolved_path_size, "%s", candidates[i]);
            return conf_file;
        }
    }

    fprintf(stderr, "Error opening config file. Tried: %s", LOCAL_CONF_NAME);
    if (candidates[1] != 0)
        fprintf(stderr, ", %s", candidates[1]);
    fprintf(stderr, ", %s\n", SYSTEM_CONF_NAME);
    exit(5);
}

struct conf_data parse_config() {
    struct conf_data data = {0};
    char *buf = 0;
    int c;
    char conf_path[PATH_MAX];
    FILE *conf_file = open_config_file(conf_path, sizeof(conf_path));
    enum READMODE mode = WHITESPACE;
    enum READMODE next_mode = PROPERTY;
    enum KEY_ID key_id = KEY_ID_COUNT;
    enum VAR_ID var_id = VAR_ID_COUNT;
    char *endptr; // for strtof()
    struct key *key = 0;

    // sane defaults
    data.vars[VAR_ID_WHEEL] = 120;
    data.vars[VAR_ID_ACCELERATION] = 0.5;
    data.vars[VAR_ID_BREAK_FACTOR] = 0.25;
    data.vars[VAR_ID_MAX_SPEED] = 12;
    data.enable_passthrough = 1;

    while ((c = getc(conf_file)) != EOF) {
        if (c == '#')
            mode = SKIPLINE;

        switch (mode) {
        case PROPERTY:
            if (is_key(buf, &key_id)) {
                next_mode = KEY_VALUE;
                mode = WHITESPACE;
                break;
            }

            if (is_var(buf, &var_id)) {
                next_mode = VAR_VALUE;
                mode = WHITESPACE;
                break;
            }

            if (strcmp(buf, DEV_ID) == 0) {
                next_mode = DEV_VALUE;
                mode = WHITESPACE;
                break;
            }

            if (strcmp(buf, ENABLE_PASSTHROUGH) == 0) {
                data.enable_passthrough = 1;
                mode = WHITESPACE;
                break;
            }

            fprintf(stderr, "Could not parse property: %s\n", buf);
            exit(5);
            break;

        case KEY_VALUE:
            key = conf_key_slot(&data, key_id);

            if (buf[0] == PASSTHROUGH) {
                key->is_pass = 1;
                if (buf[1] == '\0') {
                    mode = WHITESPACE;
                    next_mode = KEY_VALUE;
                    break;
                }
                buf++;
            }

            key->ev_code = libevdev_event_code_from_code_name(buf);
            // if (key->ev_code == -1) {
            //     fprintf(stderr, "Unknown key code name: %s\n", buf);
            //     exit(5);
            // }

            key->press = 1;
            key->release = 0;

            key->ev_type = libevdev_event_type_from_code_name(buf);

            mode = WHITESPACE;
            next_mode = RANGE;
            break;
        case VAR_VALUE:
            data.vars[var_id] = strtof(buf, &endptr);

            if (endptr == buf) {
                fprintf(stderr,
                        "%s failed parse (expected floating point compatible "
                        "number)\n",
                        buf);
                exit(5);
            }

            mode = WHITESPACE;
            next_mode = PROPERTY;
            break;
        case DEV_VALUE:
            data.dev_id = strdup(buf);
            mode = WHITESPACE;
            next_mode = PROPERTY;
            break;
        case RANGE:
            key = conf_key_slot(&data, key_id);
            if (is_range(buf, &key->release, &key->press)) {
                mode = WHITESPACE;
                next_mode = PROPERTY;
            } else {
                mode = PROPERTY;
                ungetc(c, conf_file);
            }
            break;
        case SKIPLINE:
            if (c == '\n') {
                mode = WHITESPACE;
            }
            break;
        case WHITESPACE:
            if (!isspace(c)) {
                unibuf(EOF);
                buf = unibuf(c);
                mode = READ;
            }
            break;
        case READ:
            if (!isspace(c)) {
                buf = unibuf(c);
            } else {
                mode = next_mode;
                ungetc(c, conf_file);
            }
            break;
        }
    }

    unibuf(EOF);

    fclose(conf_file);

    if (!data.enable_passthrough &&
        (data.func_keys[FUNC_ID_TOGGLE_DISABLE].ev_code == 0 &&
         data.func_keys[FUNC_ID_TOGGLE_DISABLE].ev_type == 0)) {
        if (!is_quiet)
            fprintf(stderr, "Passthrough cannot be disabled while "
                            "disable_toggle is assigned");
        exit(5);
    }

    if (is_quiet)
        return data;

    printf("\nConfig\n");
    printf("  dev_id: %s\n", data.dev_id ? data.dev_id : "");
    if (!data.enable_passthrough)
        printf("\nPassthrough disabled!\n");

    printf("\nKey bindings\n");
    printf("  %-14s %-24s %6s %6s %8s %6s\n", "action", "evdev", "code", "pass",
           "release", "press");
    printf("  %-14s %-24s %6s %6s %8s %6s\n", "--------------",
           "------------------------", "------", "------", "--------",
           "------");
    for (int key_id = 0; key_id < KEY_ID_COUNT; key_id++) {
        struct key *summary_key = conf_key_slot(&data, key_id);
        printf("  %-14s %-24s %6d %6s %8d %6d\n", key_names[key_id],
               key_repr(summary_key), summary_key->ev_code,
               summary_key->is_pass ? "yes" : "no", summary_key->release,
               summary_key->press);
    }

    printf("\nVariables\n");
    printf("  %-15s %10s\n", "name", "value");
    printf("  %-15s %10s\n", "---------------", "----------");
    for (int var_id = 0; var_id != VAR_ID_COUNT; ++var_id) {
        printf("  %-15s %10.3f\n", var_names[var_id], data.vars[var_id]);
    }

    return data;
}
