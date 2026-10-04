#pragma once
#include <stdint.h>
#ifndef WAVE_GRID
#define WAVE_GRID 40
#endif
#if WAVE_GRID != 20 && WAVE_GRID != 40
#error "WAVE_GRID must be 20 or 40"
#endif
#define WAVE_CELLS (WAVE_GRID*WAVE_GRID)
#define WAVE_RATIO ((float)WAVE_GRID/40.0f)
#define WAVE_CENTER ((float)WAVE_GRID*0.5f)
#ifndef WAVE_N
#define WAVE_N 520
#endif
#define WAVE_R (19.4f*WAVE_RATIO)
typedef struct {
    float px[WAVE_N],py[WAVE_N],ox[WAVE_N],oy[WAVE_N],speed[WAVE_N];
    int32_t qx[WAVE_N],qy[WAVE_N]; /* 邻居推挤用 Q16.16；边界仍用浮点精确投影。 */
    int16_t head[WAVE_GRID*WAVE_GRID],next[WAVE_N];
    float gx,gy,tx,ty;
    uint32_t random;
    int stage,iteration,cursor;
    float dt;
} wave_state_t;
float wave_sqrt(float value);
void wave_separate(int32_t *ax,int32_t *ay,int32_t *bx,int32_t *by);
void wave_reset(wave_state_t *s);
void wave_tilt(wave_state_t *s,int32_t x_mg,int32_t y_mg);
void wave_step(wave_state_t *s,float dt);
void wave_begin(wave_state_t *s,float dt);
int wave_work(wave_state_t *s,int batch);
void wave_splash(wave_state_t *s);
