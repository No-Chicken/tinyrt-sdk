#include "tinyrt.h"

static char label[] = "--:--";
static int32_t center_x, center_y;

static void refresh_time(void) {
    tinyrt_datetime_t time;
    if (time_local(&time, sizeof(time)) || !time.valid) {
        label[0] = label[1] = label[3] = label[4] = '-';
        return;
    }
    label[0] = '0' + time.hour / 10;
    label[1] = '0' + time.hour % 10;
    label[3] = '0' + time.minute / 10;
    label[4] = '0' + time.minute % 10;
}

int32_t tinyrt_init(int32_t width, int32_t height) {
    center_x = width / 2;
    center_y = height / 2;
    refresh_time();
    int32_t status = time_subscribe(TINYRT_TIME_MINUTE);
    return status == TINYRT_ERR_UNAVAILABLE ? 0 : status;
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if (kind == TINYRT_EV_MINUTE) refresh_time();
    return 0;
}

int32_t tinyrt_render(void) {
    if (draw_clear(0x142333)) return -1;
    return draw_text_box(center_x - 80, center_y - 24, 160, 48,
                         label, sizeof(label) - 1, 0xffffff, 36, 1);
}
