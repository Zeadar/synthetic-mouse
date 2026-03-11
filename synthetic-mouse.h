struct key {
    int code;
    int pass;
};

#define X_FOR_EACH_HOLDABLE(DO_X)                                              \
    DO_X(UP, up)                                                               \
    DO_X(DOWN, down)                                                           \
    DO_X(LEFT, left)                                                           \
    DO_X(RIGHT, right)                                                         \
    DO_X(MOUSE_BREAK, mouse_break)                                             \
    DO_X(SCROLL_DOWN, scroll_down)                                             \
    DO_X(SCROLL_UP, scroll_up)

enum HOLDABLE_ID {
// generate holdable IDs
#define GENERATE_HOLDABLE_ID(KEY_NAME_UPPER, _) HOLDABLE_ID_##KEY_NAME_UPPER,
    X_FOR_EACH_HOLDABLE(GENERATE_HOLDABLE_ID)
#undef X
        HOLDABLE_ID_COUNT,
};

#define X_FOR_EACH_KEY(DO_X)                                                   \
    X_FOR_EACH_HOLDABLE(DO_X)                                                  \
    DO_X(SCROLL_CLICK, scroll_click)                                           \
    DO_X(RIGHT_CLICK, right_click)                                             \
    DO_X(LEFT_CLICK, left_click)

enum KEY_ID {
// generate key IDs
#define GENERATE_KEY_ID(KEY_NAME_UPPER, _) KEY_ID_##KEY_NAME_UPPER,
    X_FOR_EACH_KEY(GENERATE_KEY_ID)
#undef X
        KEY_ID_COUNT,
};

static const char *key_names[KEY_ID_COUNT] = {
// generate key names
#define GENERATE_KEY_NAME(KEY_NAME_UPPER, KEY_NAME_LOWER) #KEY_NAME_UPPER "=",
    X_FOR_EACH_KEY(GENERATE_KEY_NAME)
#undef X
};

struct conf_data {
    struct key keys[KEY_ID_COUNT];
    char *phys_name;
    float acceleration;
    float break_factor;
    float max_speed;
};

struct conf_data parse_config();
