#define X_FOR_EACH_HOLDABLE(DO_X)                                              \
    DO_X(UP, up, 0)                                                            \
    DO_X(DOWN, down, 0)                                                        \
    DO_X(LEFT, left, 0)                                                        \
    DO_X(RIGHT, right, 0)                                                      \
    DO_X(MOUSE_BREAK, mouse_break, 0)                                          \
    DO_X(SCROLL_DOWN, scroll_down, 0)                                          \
    DO_X(SCROLL_UP, scroll_up, 0)

enum HOLDABLE_ID {
// generate holdable IDs
#define GENERATE_HOLDABLE_ID(KEY_NAME_UPPER, _, __)                            \
    HOLDABLE_ID_##KEY_NAME_UPPER,
    X_FOR_EACH_HOLDABLE(GENERATE_HOLDABLE_ID)
#undef GENERATE_HOLDABLE_IDX
        HOLDABLE_ID_COUNT,
};

#define X_FOR_EACH_CLICKABLE(DO_X)                                             \
    DO_X(SCROLL_CLICK, scroll_click, BTN_MIDDLE)                               \
    DO_X(RIGHT_CLICK, right_click, BTN_RIGHT)                                  \
    DO_X(LEFT_CLICK, left_click, BTN_LEFT)                                     \
    DO_X(BACKWARD, backward, BTN_SIDE)                                         \
    DO_X(FORWARD, forward, BTN_EXTRA)

enum CLICKABLE_ID {
#define GENERATE_CLICKABLE_ID(KEY_NAME_UPPER, _, __)                           \
    CLICKABLE_ID_##KEY_NAME_UPPER,
    X_FOR_EACH_CLICKABLE(GENERATE_CLICKABLE_ID)
#undef GENERATE_CLICKABLE_ID
        CLICKABLE_ID_COUNT
};

#define X_FOR_EACH_KEY(DO_X)                                                   \
    X_FOR_EACH_HOLDABLE(DO_X)                                                  \
    X_FOR_EACH_CLICKABLE(DO_X)

enum KEY_ID {
// generate key IDs
#define GENERATE_KEY_ID(KEY_NAME_UPPER, _, __) KEY_ID_##KEY_NAME_UPPER,
    X_FOR_EACH_KEY(GENERATE_KEY_ID)
#undef GENERATE_KEY_ID
        KEY_ID_COUNT,
};

#define X_FOR_EACH_VAR(DO_X)                                                   \
    DO_X(ACCELERATION, acceleration)                                           \
    DO_X(BREAK_FACTOR, break_factor)                                           \
    DO_X(MAX_SPEED, max_speed)                                                 \
    DO_X(WHEEL, wheel)

enum VAR_ID {
#define GENERATE_VAR_ID(VAR_NAME_UPPER, _) VAR_ID_##VAR_NAME_UPPER,
    X_FOR_EACH_VAR(GENERATE_VAR_ID)
#undef GENERATE_VAR_ID
        VAR_ID_COUNT
};

#define X_FOR_EACH_FUNC(DO_X) DO_X(ENABLE_TOGGLE, enable_toggle)

enum FUNC_ID {
#define GENERATE_FUNC_ID(FUNC_NAME_UPPER, _) FUNC_ID##FUNC_NAME_UPPER,
    X_FOR_EACH_FUNC(GENERATE_FUNC_ID)
#undef GENERATE_FUNC_ID
    FUNC_ID_COUNT
};

struct key {
    int ev_code;
    int ev_type;
    int is_pass;
    int release;
    int press;
};

struct conf_data {
    struct key hold_keys[HOLDABLE_ID_COUNT];
    struct key click_keys[CLICKABLE_ID_COUNT];
    struct key func_keys[FUNC_ID_COUNT];
    float vars[VAR_ID_COUNT];
    int enable_passthrough;
    char *dev_id;
};

struct conf_data parse_config();
