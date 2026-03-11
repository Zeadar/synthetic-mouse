struct conf_data {
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
    char *phys_name;
    float acceleration;
    float break_factor;
    float max_speed;
};

struct conf_data parse_config();
