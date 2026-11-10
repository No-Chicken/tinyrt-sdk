#include "tinyrt.h"

static int32_t ball_x, ball_y, width, height;
static uint32_t previous_ms;
static int have_time;

static int32_t clamp(int32_t value, int32_t upper) {
    if (value < 0) return 0;
    return value > upper ? upper : value;
}

int32_t tinyrt_init(int32_t w, int32_t h) {
    width = w;
    height = h;
    ball_x = (w - 18) * 50;
    ball_y = (h - 18) * 50;
    return imu_config(60, TINYRT_IMU_GYRO);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    (void)arg;
    if (kind != TINYRT_EV_GYRO) return 0;
    tinyrt_imu_t sample;
    if (imu_read(&sample, sizeof(sample)) != 0) return 0;
    uint32_t now = sample.timestamp_ms;
    uint32_t dt = now - previous_ms;
    if (have_time && dt <= 100) {
        ball_x = clamp(ball_x + y * (int32_t)dt / 100, (width - 18) * 100);
        ball_y = clamp(ball_y + x * (int32_t)dt / 100, (height - 18) * 100);
    }
    have_time = 1;
    previous_ms = now;
    return 0;
}

int32_t tinyrt_render(void) {
    if (draw_clear(0x172735)) return -1;
    return draw_round_rect(ball_x / 100, ball_y / 100, 18, 18, 9, 0xf8c86b);
}
