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
#include <unistd.h>
#include <time.h>

#define PHYS_NAME "usb-0000:2f:00.3-1.3/input0"
#define EVENTPATH "/dev/input/event"

const float ACCELERATION = 0.5;
const float MAX_SPEED = 12;
const float BREAK_FACTOR = 0.3;

#define SYNTH_KEYBOARD_NAME "Synthetic Passthrough"
#define SYNTH_MOUSE_NAME "Synthetic Mouse"

#define UP "KEY_KP8"
#define DOWN "KEY_KP5"
#define LEFT "KEY_KP4"
#define RIGHT "KEY_KP6"
#define RIGHTCLICK "KEY_KP9"
#define LEFTCLICK "KEY_KP7"
#define SCROLLDOWN "KEY_KP3"
#define SCROLLUP "KEY_KP1"
#define SCROLLCLICK "KEY_KP2"
#define MOUSEBREAK "KEY_KP0"

struct listen_key_codes {
    int up;
    int down;
    int left;
    int right;
    int right_click;
    int left_click;
    int scroll_down;
    int scroll_up;
    int scroll_click;
    int mouse_break;
};

struct motion_state {
    unsigned char up;
    unsigned char down;
    unsigned char left;
    unsigned char right;
    unsigned char mouse_break;
};

struct v2 {
    float x;
    float y;
};

struct libevdev *current_device;
struct libevdev_uinput *synthetic_keyboard;
struct libevdev_uinput *synthetic_mouse;
struct v2 mouse_acc = {0};
struct motion_state motion_state = {0};
pthread_mutex_t mouse_lock;
pthread_t thread_id;

void exit_handler(int sig) {
    printf("\nRecieved sig %d, exiting...\n", sig);
    pthread_cancel(thread_id);
    pthread_join(thread_id, 0);
    pthread_mutex_destroy(&mouse_lock);
    libevdev_free(current_device);
    libevdev_uinput_destroy(synthetic_keyboard);
    libevdev_uinput_destroy(synthetic_mouse);
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

void debug_print_event(struct input_event *ev) {
    printf("Event: %s %s %d\n", libevdev_event_type_get_name(ev->type),
           libevdev_event_code_get_name(ev->type, ev->code), ev->value);
}

void *mouse_handler() {
    float max = MAX_SPEED;
    for (;;) {
        pthread_mutex_lock(&mouse_lock);
        if (motion_state.up)
            mouse_acc.y += -ACCELERATION;
        else if (motion_state.down)
            mouse_acc.y += ACCELERATION;
        else {
            mouse_acc.y = 0;
        }
        if (motion_state.left)
            mouse_acc.x += -ACCELERATION;
        else if (motion_state.right)
            mouse_acc.x += ACCELERATION;
        else {
            mouse_acc.x = 0;
        }
        if(motion_state.mouse_break)
            max = MAX_SPEED * BREAK_FACTOR;
        else
            max = MAX_SPEED;
        pthread_mutex_unlock(&mouse_lock);

        if (mouse_acc.x > max)
            mouse_acc.x = max;

        if (mouse_acc.x < -max)
            mouse_acc.x = -max;

        if (mouse_acc.y > max)
            mouse_acc.y = max;

        if (mouse_acc.y < -max)
            mouse_acc.y = -max;

        libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_X,
                                    (int) roundf(mouse_acc.x));
        libevdev_uinput_write_event(synthetic_mouse, EV_REL, REL_Y,
                                    (int) roundf(mouse_acc.y));
        libevdev_uinput_write_event(synthetic_mouse, EV_SYN, SYN_REPORT, 0);

        usleep(1000 * 10);
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

    pthread_mutex_init(&mouse_lock, 0);
    // const int listen_type = libevdev_event_type_from_name(LISTEN_EVENT);

    if (argc > 1)
        if (strcmp(argv[1], "--list") == 0) {
            get_device_by_phys_name("", 1);
            return 0;
        }

    struct listen_key_codes key_codes = {
        .up = libevdev_event_code_from_code_name(UP),
        .down = libevdev_event_code_from_code_name(DOWN),
        .left = libevdev_event_code_from_code_name(LEFT),
        .right = libevdev_event_code_from_code_name(RIGHT),
        .right_click = libevdev_event_code_from_code_name(RIGHTCLICK),
        .left_click = libevdev_event_code_from_code_name(LEFTCLICK),
        .scroll_down = libevdev_event_code_from_code_name(SCROLLDOWN),
        .scroll_up = libevdev_event_code_from_code_name(SCROLLUP),
        .scroll_click = libevdev_event_code_from_code_name(SCROLLCLICK),
        .mouse_break = libevdev_event_code_from_code_name(MOUSEBREAK),
    };

    printf("%d %d %d %d\n", key_codes.up, key_codes.down, key_codes.left,
           key_codes.right);

    current_device = get_device_by_phys_name(PHYS_NAME, 0);
    int rc;

    if (!current_device)
        return 10;

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
    libevdev_uinput_create_from_device(
        temp_synth_mouse, LIBEVDEV_UINPUT_OPEN_MANAGED, &synthetic_mouse);
    libevdev_free(temp_synth_mouse);

    libevdev_grab(current_device, LIBEVDEV_GRAB);

    thread_id = pthread_create(&thread_id, 0, mouse_handler, 0);

    do {
        struct input_event ev;
        rc =
            libevdev_next_event(current_device, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc != 0) {
            fprintf(stderr, "%s\n", strerror(-rc));
            continue;
        }

        if (ev.type == EV_KEY) {
            if (ev.code == key_codes.up) {
                pthread_mutex_lock(&mouse_lock);
                if (ev.value == 1)
                    motion_state.up = 1;
                else if (ev.value == 0)
                    motion_state.up = 0;
                pthread_mutex_unlock(&mouse_lock);
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.down) {
                pthread_mutex_lock(&mouse_lock);
                if (ev.value == 1)
                    motion_state.down = 1;
                else if (ev.value == 0)
                    motion_state.down = 0;
                pthread_mutex_unlock(&mouse_lock);
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.left) {
                pthread_mutex_lock(&mouse_lock);
                if (ev.value == 1)
                    motion_state.left = 1;
                else if (ev.value == 0)
                    motion_state.left = 0;
                pthread_mutex_unlock(&mouse_lock);
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.right) {
                pthread_mutex_lock(&mouse_lock);
                if (ev.value == 1)
                    motion_state.right = 1;
                else if (ev.value == 0)
                    motion_state.right = 0;
                pthread_mutex_unlock(&mouse_lock);
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.right_click) {
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.left_click) {
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.scroll_down) {
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.scroll_up) {
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.scroll_click) {
                debug_print_event(&ev);
                continue;
            }
            if (ev.code == key_codes.mouse_break) {
                pthread_mutex_lock(&mouse_lock);
                if (ev.value == 1)
                    motion_state.mouse_break = 1;
                else if (ev.value == 0)
                    motion_state.mouse_break = 0;
                pthread_mutex_unlock(&mouse_lock);
                debug_print_event(&ev);
                continue;
            }
        }
        libevdev_uinput_write_event(synthetic_keyboard, ev.type, ev.code,
                                    ev.value);
    } while (rc == 1 || rc == 0 || rc == -EAGAIN);

    return 0;
}
