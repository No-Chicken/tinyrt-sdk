#pragma once
#include <stdint.h>
#define WAVE_GRID 40
#ifndef WAVE_N
#define WAVE_N 520
#endif
#define WAVE_R 19.4f
typedef struct {
    float px[WAVE_N],py[WAVE_N],ox[WAVE_N],oy[WAVE_N],speed[WAVE_N];
    int16_t head[WAVE_GRID*WAVE_GRID],next[WAVE_N];
    float gx,gy,tx,ty;
    uint32_t random;
    int stage,iteration,cursor;
    float dt;
} wave_state_t;
float wave_sqrt(float value);
void wave_reset(wave_state_t *s);
void wave_tilt(wave_state_t *s,int32_t x_mg,int32_t y_mg);
void wave_step(wave_state_t *s,float dt);
void wave_begin(wave_state_t *s,float dt);
int wave_work(wave_state_t *s,int batch);
void wave_splash(wave_state_t *s);
