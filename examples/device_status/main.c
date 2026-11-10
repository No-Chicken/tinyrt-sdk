#include "tinyrt.h"

static int32_t center_x, center_y;
static unsigned bright;

int32_t tinyrt_init(int32_t width, int32_t height) {
    center_x = width / 2;
    center_y = height / 2;
    if (input_events(0)) return -1;
    int32_t result = time_subscribe(TINYRT_TIME_MINUTE);
    return result == TINYRT_ERR_UNAVAILABLE ? 0 : result;
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if (kind != TINYRT_TOUCH_RELEASE) return 0;
    bright = !bright;
    if (display_brightness(bright ? 70 : 35) == TINYRT_ERR_UNAVAILABLE) bright = 0;
    screen_keep_on(bright);
    return 0;
}

int32_t tinyrt_render(void) {
    tinyrt_datetime_t time;
    tinyrt_battery_t battery;
    char line[] = "--:--   --- %";
    if (time_local(&time, sizeof(time)) == 0 && time.valid) {
        line[0] = '0' + time.hour / 10;
        line[1] = '0' + time.hour % 10;
        line[3] = '0' + time.minute / 10;
        line[4] = '0' + time.minute % 10;
    }
    if (battery_info(&battery, sizeof(battery)) == 0) {
        int32_t p = battery.level_pct;
        line[8] = p == 100 ? '1' : ' ';
        line[9] = p >= 10 ? '0' + p / 10 % 10 : ' ';
        line[10] = '0' + p % 10;
    }
    if (draw_clear(0x142333)) return -1;
    if (draw_text_box(center_x-140, center_y-40, 280, 48, line, sizeof(line)-1,
                      0xffffff, 36, 1)) return -1;
    const char *hint = bright ? "Tap: dim" : "Tap: brighten";
    return draw_text_box(center_x-100, center_y+12, 200, 32, hint, bright ? 8 : 13,
                         0xaad5ea, 18, 1);
}
