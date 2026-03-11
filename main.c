#include "synthetic-mouse.h"
#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EVENTPATH "/dev/input/event"

#define SYNTH_KEYBOARD_NAME "Synthetic Passthrough"
#define SYNTH_MOUSE_NAME "Synthetic Mouse"

struct holdable_state {
    unsigned char held[HOLDABLE_ID_COUNT];
};

struct motion_state {
#define GENERATE_FIELD(_, KEY_NAME_LOWER) unsigned char KEY_NAME_LOWER;
    X_FOR_EACH_KEY(GENERATE_FIELD)
#undef GENERATE_FIELD
};

struct v2 {
    float x;
    float y;
};

struct conf_data conf_data = {0};
struct libevdev *current_device;
struct libevdev_uinput *synthetic_keyboard;
struct libevdev_uinput *synthetic_mouse;
struct v2 input_direction = {0};
struct motion_state motion_state = {0};
struct holdable_state holdable_state = {0};

pthread_mutex_t motion_lock;
pthread_t thread_id;
volatile sig_atomic_t thread_running = 0;

struct v2 v2_normalize(const struct v2 v) {
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len == 0)
        return (struct v2) {0, 0};
    return (struct v2) {
        .x = v.x / len,
        .y = v.y / len,
    };
}

struct v2 v2_scale(const struct v2 v, float s) {
    return (struct v2) {
        .x = v.x * s,
        .y = v.y * s,
    };
}

void exit_handler(int sig) {
    printf("\nRecieved sig %d, exiting...\n", sig);
    if (thread_running) {
        pthread_cancel(thread_id);
        pthread_join(thread_id, 0);
    }
    pthread_mutex_destroy(&motion_lock);
    libevdev_free(current_device);
    libevdev_uinput_destroy(synthetic_keyboard);
    libevdev_uinput_destroy(synthetic_mouse);
    free(conf_data.phys_name);
    exit(0);
}

struct libevdev *get_device_by_phys_name(char *phys_name, int verbose) {
    int event_suffix = 0;
    int evdev_fd;
    char *path_buf = 0;
    char *suffix_buf = 0;
    struct libevdev *device;

    for (;;) {
        size_t sbufsize = event_suffix ? log10(event_suffix) + 2 : 2;
        suffix_buf = realloc(suffix_buf, sbufsize);
        sprintf(suffix_buf, "%d", event_suffix++);
        path_buf = realloc(path_buf, strlen(EVENTPATH) + sbufsize);
        strcpy(path_buf, EVENTPATH);
        strcat(path_buf, suffix_buf);
        evdev_fd = open(path_buf, O_RDWR | O_CLOEXEC);
        // evdev_fd = open(path_buf, O_RDONLY | O_NONBLOCK);

        if (evdev_fd == -1)
            break;

        if (libevdev_new_from_fd(evdev_fd, &device) == 0) {
            const char *phys = libevdev_get_phys(device);
            const char *name = libevdev_get_name(device);

            if (verbose)
                printf("%s\n\tname:\t\t:%s:\n\tphys_name:\t%s\n", path_buf,
                       name, phys);

            if (strcmp(phys, phys_name) == 0) {
                // close(evdev_fd);
                free(path_buf);
                free(suffix_buf);
                return device;
            } else {
                libevdev_free(device);
            }
        }

        close(evdev_fd);
    };

    free(path_buf);
    free(suffix_buf);
    return 0;
}

void log_event(struct input_event *ev) {
    // libevdev_event_value_get_name(unsigned int type, unsigned int code, int
    // value)

    printf("Type: %-7sCode: %-16sValue: %d\n",
           libevdev_event_type_get_name(ev->type),
           libevdev_event_code_get_name(ev->type, ev->code), ev->value);
    // libevdev_event_value_get_name(ev->type, ev->code, ev->value));
}

void *mouse_handler() {
    float max = conf_data.max_speed;
    float speed = 0;
    int cycles = 0;
    const long frame_ns = 1000000000L / 100;
    struct timespec next_tick;
    clock_gettime(CLOCK_MONOTONIC, &next_tick);

    for (;;) {
        pthread_mutex_lock(&motion_lock);

        if (memcmp(&motion_state, &(const struct motion_state) {0},
                   sizeof(struct motion_state)) == 0) {
            if (cycles++ == 100 * 60) {
                thread_running = 0;
                pthread_mutex_unlock(&motion_lock);
                pthread_exit(0);
            }
        } else {
            cycles = 0;
        }

        if (motion_state.up)
            input_direction.y += -1;
        else if (motion_state.down)
            input_direction.y += 1;
        else {
            input_direction.y = 0;
        }
        if (motion_state.left)
            input_direction.x = -1;
        else if (motion_state.right)
            input_direction.x = 1;
        else {
            input_direction.x = 0;
        }

        if (motion_state.mouse_break) {
            max = conf_data.max_speed * conf_data.break_factor;
        } else {
            max = conf_data.max_speed;
        }

        if (motion_state.up || motion_state.down || motion_state.left ||
            motion_state.right) {
            speed += conf_data.acceleration;
            speed = speed > max ? max : speed;
        } else {
            speed = 0;
        }

        input_direction = v2_normalize(input_direction);
        struct v2 velocity = v2_scale(input_direction, speed);

        if (memcmp(&motion_state, &(const struct motion_state) {0},
                   sizeof motion_state) != 0) {
            libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_X,
                                        (int) roundf(velocity.x));
            libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_Y,
                                        (int) roundf(velocity.y));
            if (motion_state.scroll_up)
                libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_WHEEL,
                                            1);
            if (motion_state.scroll_down)
                libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_WHEEL,
                                            -1);
            libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT, 0);
        }

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

int main(int argc, char **argv) {
    struct sigaction sa = {0};
    sa.sa_handler = exit_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    int key_logging = 0;

    pthread_mutex_init(&motion_lock, 0);

    for (int i = 1; i != argc; ++i) {
        if (strcmp(argv[i], "--list-devices") == 0)
            get_device_by_phys_name("", 1);
        if (strcmp(argv[i], "--log-keys") == 0)
            key_logging = 1;
    }

    conf_data = parse_config();

    current_device = get_device_by_phys_name(conf_data.phys_name, 0);
    int rc;

    if (!current_device) {
        fprintf(stderr, "Device %s not found.", conf_data.phys_name);
        return 10;
    }

    // passthrough setup
    struct libevdev *temp_synth_keyboard = libevdev_new();
    libevdev_set_name(temp_synth_keyboard, SYNTH_KEYBOARD_NAME);
    libevdev_enable_event_type(temp_synth_keyboard, EV_SYN);
    libevdev_enable_event_type(temp_synth_keyboard, EV_KEY);
    for (int key_code = 0; key_code != libevdev_event_type_get_max(EV_KEY);
         ++key_code)
        if (libevdev_has_event_code(current_device, EV_KEY, key_code))
            libevdev_enable_event_code(temp_synth_keyboard, EV_KEY, key_code,
                                       0);
    libevdev_uinput_create_from_device(
        temp_synth_keyboard, LIBEVDEV_UINPUT_OPEN_MANAGED, &synthetic_keyboard);
    libevdev_free(temp_synth_keyboard);

    // mouse setup
    struct libevdev *temp_synth_mouse = libevdev_new();
    libevdev_set_name(temp_synth_mouse, SYNTH_MOUSE_NAME);
    libevdev_enable_property(temp_synth_mouse, INPUT_PROP_POINTER);
    libevdev_enable_event_type(temp_synth_mouse, EV_REL);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_X, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_Y, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_REL, REL_WHEEL, 0);
    libevdev_enable_event_type(temp_synth_mouse, EV_KEY);
    libevdev_enable_event_code(temp_synth_mouse, EV_KEY, BTN_LEFT, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_KEY, BTN_RIGHT, 0);
    libevdev_enable_event_code(temp_synth_mouse, EV_KEY, BTN_MIDDLE, 0);
    libevdev_uinput_create_from_device(
        temp_synth_mouse, LIBEVDEV_UINPUT_OPEN_MANAGED, &synthetic_mouse);
    libevdev_free(temp_synth_mouse);

    libevdev_grab(current_device, LIBEVDEV_GRAB);

    // thread_id = pthread_create(&thread_id, 0, mouse_handler, 0);

    do {
        if (!thread_running) {
            thread_running = 1;
            pthread_create(&thread_id, 0, mouse_handler, 0);
        }

        struct input_event ev;
        rc =
            libevdev_next_event(current_device, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc != 0) {
            fprintf(stderr, "%s\n", strerror(-rc));
            continue;
        }

        if (ev.type == EV_KEY) {
            if (key_logging)
                log_event(&ev);

            // for (int i = 0; i < HOLDABLE_ID_COUNT; i++) {
            //     if (ev.code == conf_data.keys[i].code) {
            //         pthread_mutex_lock(&mouse_lock);
            //         if (ev.value == 1) {
            //             holdable_state.held[i] = 1;
            //         } else if (ev.value == 0) {
            //             holdable_state.held[i] = 0;
            //         }
            //         pthread_mutex_unlock(&mouse_lock);
            //         if (conf_data.keys[i].pass)
            //             goto passthrough;
            //         continue;
            //     }
            // }
            if (ev.code == conf_data.keys[KEY_ID_UP].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1)
                    motion_state.up = 1;
                else if (ev.value == 0)
                    motion_state.up = 0;
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_UP].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_DOWN].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1)
                    motion_state.down = 1;
                else if (ev.value == 0)
                    motion_state.down = 0;
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_DOWN].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_LEFT].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1)
                    motion_state.left = 1;
                else if (ev.value == 0)
                    motion_state.left = 0;
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_LEFT].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_RIGHT].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1)
                    motion_state.right = 1;
                else if (ev.value == 0)
                    motion_state.right = 0;
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_RIGHT].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_RIGHT_CLICK].code) {
                libevdev_uinput_write_event(synthetic_mouse, EV_KEY, BTN_RIGHT,
                                            ev.value);
                libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT,
                                            0);
                if (conf_data.keys[KEY_ID_RIGHT_CLICK].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_LEFT_CLICK].code) {
                libevdev_uinput_write_event(synthetic_mouse, EV_KEY, BTN_LEFT,
                                            ev.value);
                libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT,
                                            0);
                if (conf_data.keys[KEY_ID_LEFT_CLICK].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_SCROLL_DOWN].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1) {
                    motion_state.scroll_down = 1;
                } else if (ev.value == 0) {
                    motion_state.scroll_down = 0;
                }
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_SCROLL_DOWN].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_SCROLL_UP].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1) {
                    motion_state.scroll_up = 1;
                } else if (ev.value == 0) {
                    motion_state.scroll_up = 0;
                }
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_SCROLL_UP].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_SCROLL_CLICK].code) {
                pthread_mutex_lock(&motion_lock);
                libevdev_uinput_write_event(synthetic_mouse, EV_KEY, BTN_MIDDLE,
                                            ev.value);
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_SCROLL_CLICK].pass)
                    goto passthrough;
                continue;
            }
            if (ev.code == conf_data.keys[KEY_ID_MOUSE_BREAK].code) {
                pthread_mutex_lock(&motion_lock);
                if (ev.value == 1)
                    motion_state.mouse_break = 1;
                else if (ev.value == 0)
                    motion_state.mouse_break = 0;
                pthread_mutex_unlock(&motion_lock);
                if (conf_data.keys[KEY_ID_MOUSE_BREAK].pass)
                    goto passthrough;
                continue;
            }
        }
    passthrough:
        libevdev_uinput_write_event(synthetic_keyboard, ev.type, ev.code,
                                    ev.value);
    } while (rc == 1 || rc == 0 || rc == -EAGAIN);

    return 0;
}
