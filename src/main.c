#include "synthetic-mouse.h"
#include <asm-generic/errno-base.h>
#include <dirent.h>
#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EVENTPATH "/dev/input/by-id/"
#define F_LOG "%-18sType: %-18sCode: %-18sValue: %d"

#define PASSTHROUGH_FRAME_MAX 32
#define SYNTH_PASSTHROUGH_NAME "Synthetic Passthrough"
#define SYNTH_MOUSE_NAME "Synthetic Mouse"
#define HELP                                                                   \
    "Usage: synthetic-mouse [options]\n"                                       \
    "\n"                                                                       \
    "Reads configuration from ./synthetic.conf, "                              \
    "$XDG_CONFIG_HOME/synthetic-mouse/\n"                                      \
    "synthetic.conf, or /etc/synthetic-mouse/synthetic.conf,\n"                \
    "opens the configured input device, and emits synthetic mouse events.\n"   \
    "\n"                                                                       \
    "Options:\n"                                                               \
    "  --list-devices  List /dev/input/by-id devices and exit.\n"              \
    "  --log-input     Print source input events.\n"                           \
    "  --log-output    Print synthetic output events.\n"                       \
    "  --log-pass      Print passthrough output events.\n"                     \
    "  --quiet         Suppress non-critical diagnostics.\n"                   \
    "  --help          Show this help text.\n"

struct v2 {
    float x;
    float y;
};

struct conf_data conf_data = {0};
struct libevdev *current_device;
struct libevdev_uinput *synthetic_passthrough;
struct libevdev_uinput *synthetic_mouse;
float motion_state[HOLDABLE_ID_COUNT];
int click_action[CLICKABLE_ID_COUNT] = {
#define GENERATE_CLICK_ACTION(_, __, BUTTON_CODE) BUTTON_CODE,
    X_FOR_EACH_CLICKABLE(GENERATE_CLICK_ACTION)
#undef GENERATE_CLICK_ACTION
};

pthread_mutex_t motion_lock;
pthread_t thread_id;
_Atomic int is_thread_running = 0;
volatile sig_atomic_t shutdown_requested = 0;
int is_quiet = 0;
int is_disabled = 0;
int is_output_log = 0;
int is_force_passthrough = 0;

struct v2 v2_normalize(const struct v2 v) {
    const float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len == 0)
        return (struct v2) {0, 0};
    return (struct v2) {
        .x = v.x / len,
        .y = v.y / len,
    };
}

struct v2 v2_scale(const struct v2 v, const float s) {
    return (struct v2) {
        .x = v.x * s,
        .y = v.y * s,
    };
}

void log_event(struct input_event *ev, const char *setting) {
    printf(F_LOG "\n", setting, libevdev_event_type_get_name(ev->type),
           libevdev_event_code_get_name(ev->type, ev->code), ev->value);
}

// I hate this solution
int log_write_event(const struct libevdev_uinput *uinput_dev, unsigned int type,
                    unsigned int code, int value) {
    if (is_output_log)
        printf(F_LOG "\n", "output", libevdev_event_type_get_name(type),
               libevdev_event_code_get_name(type, code), value);

    return libevdev_uinput_write_event(uinput_dev, type, code, value);
}

static void inherit_device_caps(struct libevdev *dst,
                                const struct libevdev *src) {
    // Copy basic identity so the uinput clone looks like the source device.
    libevdev_set_id_bustype(dst, libevdev_get_id_bustype(src));
    libevdev_set_id_vendor(dst, libevdev_get_id_vendor(src));
    libevdev_set_id_product(dst, libevdev_get_id_product(src));
    libevdev_set_id_version(dst, libevdev_get_id_version(src));

    // Copy input properties (e.g. pointer/touchpad).
    for (int prop = 0; prop <= INPUT_PROP_MAX; ++prop) {
        if (libevdev_has_property(src, prop))
            libevdev_enable_property(dst, prop);
    }

    // Always enable SYN_REPORT; uinput devices need it for event frames.
    libevdev_enable_event_type(dst, EV_SYN);
    libevdev_enable_event_code(dst, EV_SYN, SYN_REPORT, 0);

    // Copy supported event types and their codes.
    for (int type = 0; type <= EV_MAX; ++type) {
        if (!libevdev_has_event_type(src, type))
            continue;

        libevdev_enable_event_type(dst, type);

        int max_code = libevdev_event_type_get_max(type);
        if (max_code <= 0)
            continue;
        for (int code = 0; code != max_code; ++code) {
            if (!libevdev_has_event_code(src, type, code))
                continue;

            if (type == EV_ABS) {
                const struct input_absinfo *abs =
                    libevdev_get_abs_info(src, code);
                libevdev_enable_event_code(dst, type, code, abs);
            } else {
                libevdev_enable_event_code(dst, type, code, 0);
            }
        }
    }
}

static void cleanup_and_exit(int exit_code) {
    if (atomic_load(&is_thread_running)) {
        pthread_mutex_lock(&motion_lock);
        pthread_cancel(thread_id);
        pthread_mutex_unlock(&motion_lock);
        pthread_join(thread_id, 0);
    }
    pthread_mutex_destroy(&motion_lock);
    libevdev_free(current_device);
    if (conf_data.enable_passthrough)
        libevdev_uinput_destroy(synthetic_passthrough);
    libevdev_uinput_destroy(synthetic_mouse);
    free(conf_data.dev_id);
    exit(exit_code);
}

void exit_handler(int sig) {
    shutdown_requested = sig;
}

struct libevdev *get_device_by_id(char *dev_id, int verbose) {
    DIR *dir;
    struct dirent *entry;
    int evdev_fd;
    struct libevdev *device;

    dir = opendir(EVENTPATH);
    if (dir == 0) {
        fprintf(stderr, "Can't open %s\n", EVENTPATH);
        perror("opendir");
        exit(1);
    }

    while ((entry = readdir(dir)) != 0) {
        if (entry->d_name[0] == '.')
            continue;

        char *full_path =
            calloc(strlen(entry->d_name) + strlen(EVENTPATH) + 1, sizeof(char));
        strcat(full_path, EVENTPATH);
        strcat(full_path, entry->d_name);

        evdev_fd = open(full_path, O_RDWR | O_CLOEXEC);
        free(full_path);

        if (evdev_fd == -1) {
            if (verbose)
                printf("%s: FAILED to open\n", entry->d_name);
            continue;
        }

        if (libevdev_new_from_fd(evdev_fd, &device) == 0) {
            const char *phys = libevdev_get_phys(device);
            const char *name = libevdev_get_name(device);

            if (verbose)
                printf("\n\tdev_id:\t%s\n\tname:\t\t|%s|\n\tphys_name:\t%s\n",
                       entry->d_name, name, phys);

            if (strcmp(entry->d_name, dev_id) == 0) {
                closedir(dir);
                return device;
            } else {
                libevdev_free(device);
            }
        }

        close(evdev_fd);
    }

    closedir(dir);

    return 0;
}

void *mouse_handler() {
    struct v2 input_direction = {0};
    struct v2 velocity = {0};
    float max = conf_data.vars[VAR_ID_MAX_SPEED];
    float speed = 0;
    float mb = 0;
    int cycles = 0;
    int is_x = 0;
    int is_y = 0;
    int is_s = 0;

    const long frame_ns = 1000000000L / 100;
    struct timespec next_tick;
    clock_gettime(CLOCK_MONOTONIC, &next_tick);

    for (;;) {
        pthread_mutex_lock(&motion_lock);

        if (memcmp(&motion_state, &(const float[HOLDABLE_ID_COUNT]) {0},
                   sizeof(motion_state)) == 0) {
            if (cycles++ == 100 * 60) {
                atomic_store(&is_thread_running, 0);
                pthread_mutex_unlock(&motion_lock);
                pthread_exit(0);
            }
        } else {
            cycles = 0;
        }

        if ((is_y = motion_state[HOLDABLE_ID_UP]))
            input_direction.y = -1 * motion_state[HOLDABLE_ID_UP];
        else if ((is_y = motion_state[HOLDABLE_ID_DOWN]))
            input_direction.y = 1 * motion_state[HOLDABLE_ID_DOWN];
        else {
            input_direction.y = 0;
        }

        if ((is_x = motion_state[HOLDABLE_ID_LEFT]))
            input_direction.x = -1 * motion_state[HOLDABLE_ID_LEFT];
        else if ((is_x = motion_state[HOLDABLE_ID_RIGHT]))
            input_direction.x = 1 * motion_state[HOLDABLE_ID_RIGHT];
        else {
            input_direction.x = 0;
        }

        if (motion_state[HOLDABLE_ID_MOUSE_BREAK]) {
            max = conf_data.vars[VAR_ID_MAX_SPEED] *
                  conf_data.vars[VAR_ID_MOUSE_BREAK_FACTOR];
        } else {
            max = conf_data.vars[VAR_ID_MAX_SPEED];
        }

        if (is_x || is_y) {
            speed += conf_data.vars[VAR_ID_ACCELERATION];
            speed = speed > max ? max : speed;
            velocity = v2_scale(v2_normalize(input_direction), speed);
            velocity.x *= fabsf(input_direction.x);
            velocity.y *= fabsf(input_direction.y);
        } else {
            speed = 0;
        }

        if (is_x)
            log_write_event(synthetic_mouse, EV_REL, REL_X,
                            (int) roundf(velocity.x));

        if (is_y)
            log_write_event(synthetic_mouse, EV_REL, REL_Y,
                            (int) roundf(velocity.y));

        mb = motion_state[HOLDABLE_ID_MOUSE_BREAK]
                 ? conf_data.vars[VAR_ID_SCROLL_BREAK_FACTOR]
                 : 1;

        if ((is_s = motion_state[HOLDABLE_ID_SCROLL_UP])) {
            log_write_event(synthetic_mouse, EV_REL, REL_WHEEL_HI_RES,
                            (int) roundf(conf_data.vars[VAR_ID_WHEEL] *
                                         motion_state[HOLDABLE_ID_SCROLL_UP] *
                                         mb));
        } else if ((is_s = motion_state[HOLDABLE_ID_SCROLL_DOWN])) {
            log_write_event(
                synthetic_mouse, EV_REL, REL_WHEEL_HI_RES,
                -(int) roundf(conf_data.vars[VAR_ID_WHEEL] *
                              motion_state[HOLDABLE_ID_SCROLL_DOWN] * mb));
        }

        if (is_x || is_y || is_s)
            libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT, 0);

        pthread_mutex_unlock(&motion_lock);

        next_tick.tv_nsec += frame_ns;
        if (next_tick.tv_nsec >= 1000000000L) {
            next_tick.tv_sec += next_tick.tv_nsec / 1000000000L;
            next_tick.tv_nsec %= 1000000000L;
        }

        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_tick, 0) !=
            0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            next_tick = now;
        }
    }

    return 0;
}

void wake_thread() {
    if (!atomic_exchange(&is_thread_running, 1))
        if (pthread_create(&thread_id, 0, mouse_handler, 0) != 0)
            atomic_store(&is_thread_running, 0);
}

int main(int argc, char **argv) {
    struct sigaction sa = {0};
    sa.sa_handler = exit_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    int is_input_log = 0;
    int is_pass_log = 0;

    for (int i = 1; i != argc; ++i) {
        if (strcmp(argv[i], "--list-devices") == 0) {
            get_device_by_id("", 1);
            exit(0);
        }
        if (strcmp(argv[i], "--help") == 0) {
            printf(HELP "\n");
            exit(0);
        }
        if (strcmp(argv[i], "--log-input") == 0) {
            is_input_log = 1;
            continue;
        }
        if (strcmp(argv[i], "--log-output") == 0) {
            is_output_log = 1;
            continue;
        }
        if (strcmp(argv[i], "--log-pass") == 0) {
            is_pass_log = 1;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0) {
            is_quiet = 1;
            continue;
        }
        fprintf(stderr, "Unrecognized: %s\n", argv[i]);
    }

    pthread_mutex_init(&motion_lock, 0);

    conf_data = parse_config();

    current_device = get_device_by_id(conf_data.dev_id, 0);
    int rc;

    if (!current_device) {
        fprintf(stderr, "Device %s not found.\n", conf_data.dev_id);
        return 10;
    }

    // passthrough setup
    if (conf_data.enable_passthrough) {
        struct libevdev *temp_synth_pass = libevdev_new();
        libevdev_set_name(temp_synth_pass, SYNTH_PASSTHROUGH_NAME);
        inherit_device_caps(temp_synth_pass, current_device);
        libevdev_uinput_create_from_device(temp_synth_pass,
                                           LIBEVDEV_UINPUT_OPEN_MANAGED,
                                           &synthetic_passthrough);
        libevdev_free(temp_synth_pass);
    }

    // mouse setup
    struct libevdev *temp_synth_mouse = libevdev_new();
    libevdev_set_name(temp_synth_mouse, SYNTH_MOUSE_NAME);
    libevdev_enable_property(temp_synth_mouse, INPUT_PROP_POINTER);
    libevdev_enable_event_type(temp_synth_mouse, EV_REL);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_X, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_Y, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_WHEEL_HI_RES, 0);
    libevdev_enable_event_type(temp_synth_mouse, EV_KEY);
#define ENABLE_CLICKABLE_BUTTON(_, __, BUTTON_CODE)                            \
    libevdev_enable_event_code(temp_synth_mouse, EV_KEY, BUTTON_CODE, 0);
    X_FOR_EACH_CLICKABLE(ENABLE_CLICKABLE_BUTTON)
#undef ENABLE_CLICKABLE_BUTTON
    libevdev_enable_event_type(temp_synth_mouse, EV_SYN);
    libevdev_enable_event_code(temp_synth_mouse, EV_SYN, SYN_REPORT, 0);

    libevdev_uinput_create_from_device(
        temp_synth_mouse, LIBEVDEV_UINPUT_OPEN_MANAGED, &synthetic_mouse);
    libevdev_free(temp_synth_mouse);

    libevdev_grab(current_device, LIBEVDEV_GRAB);

    do {
        struct input_event ev;
        rc =
            libevdev_next_event(current_device, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc != 0) {
            if (shutdown_requested && rc == -EINTR)
                break;
            if (!is_quiet)
                fprintf(stderr, "%s\n", strerror(-rc));
            continue;
        }

        if (is_input_log)
            log_event(&ev, "input");

        for (int key_id = 0; key_id != FUNC_ID_COUNT; ++key_id) {
            struct key *key = &conf_data.func_keys[key_id];

            if (!(ev.code == key->ev_code && ev.type == key->ev_type))
                continue;

            if (key->press == ev.value) {
                if (key_id == FUNC_ID_TOGGLE_DISABLE) {
                    is_force_passthrough = !is_force_passthrough;
                }

                if (key->is_pass)
                    goto passthrough;
                else
                    goto endpoint;
            }
        }

        if (is_force_passthrough) {
            goto passthrough;
        }

        if (ev.type == EV_SYN || ev.type == EV_MSC)
            continue;

        for (int key_id = 0; key_id < HOLDABLE_ID_COUNT; key_id++) {
            struct key *key = &conf_data.hold_keys[key_id];

            if (!(ev.code == key->ev_code && ev.type == key->ev_type))
                continue;

            const float val = ev.value;
            const float pre = key->press;
            const float rel = key->release;

            float strength = (val - rel) * (1 / (pre - rel));

            if (key->ev_type == EV_KEY) {
                if (strength > 1 || strength < 0) {
                    continue;
                }
            } else {
                if (strength > 1 || strength <= 0) {
                    pthread_mutex_lock(&motion_lock);
                    motion_state[key_id] = 0;
                    pthread_mutex_unlock(&motion_lock);
                    continue;
                }
            }

            pthread_mutex_lock(&motion_lock);
            motion_state[key_id] = strength;
            pthread_mutex_unlock(&motion_lock);

            wake_thread();

            if (key->is_pass)
                goto passthrough;
            else
                goto endpoint;
        }

        for (int key_id = 0; key_id < CLICKABLE_ID_COUNT; ++key_id) {
            struct key *key = &conf_data.click_keys[key_id];

            if (!(ev.code == key->ev_code && ev.type == key->ev_type))
                continue;

            if (key->ev_code == 0 && key->ev_type == 0)
                continue;

            const float val = ev.value;
            const float pre = key->press;
            const float rel = key->release;
            float strength = (val - rel) * (1 / (pre - rel));

            if (key->ev_type == EV_KEY && (strength > 1 || strength < 0)) {
                continue;
            }

            log_write_event(synthetic_mouse, EV_KEY, click_action[key_id],
                            ev.value == key->press);
            libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT, 0);

            if (key->is_pass)
                goto passthrough;
            else
                goto endpoint;
        }

    passthrough:

        if (!conf_data.enable_passthrough)
            continue;

        if (is_pass_log)
            log_event(&ev, "passthrough");

        libevdev_uinput_write_event(synthetic_passthrough, ev.type, ev.code,
                                    ev.value);
        libevdev_uinput_write_event(synthetic_passthrough, EV_SYN, SYN_REPORT,
                                    0);

    endpoint:
        continue; // to make the LSP stfu

    } while (rc == 1 || rc == 0 || rc == -EAGAIN);

    if (shutdown_requested) {
        if (!is_quiet)
            printf("\nRecieved sig %d, exiting...\n", shutdown_requested);
        cleanup_and_exit(0);
    }

    return 2; // loop sould not end normally
}
