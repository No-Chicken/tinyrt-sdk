#pragma once
#include <stdint.h>
#ifndef WAVE_BALL_COUNT
#define WAVE_BALL_COUNT 600
#endif
#define WAVE_BALL_BOUND 190.0f
#define WAVE_BALL_DEPTH 96.0f
#define WAVE_BALL_CONTACT_Q7 (10 * 128)
#define WAVE_BALL_GRID_X 39
#define WAVE_BALL_GRID_Z 5
#define WAVE_BALL_CELLS (WAVE_BALL_GRID_X*WAVE_BALL_GRID_X*WAVE_BALL_GRID_Z)
/* 位置 Q9.7、速度 Q11.5，缓存中的每个粒子只占 12 字节。 */
typedef struct {int16_t x,y,z,vx,vy,vz;} wave_ball_t;
typedef struct {float x,y,z,vx,vy,vz;} wave_ball_view_t;
typedef struct {
    wave_ball_t p[WAVE_BALL_COUNT];
    wave_ball_t sorted[WAVE_BALL_COUNT];
    uint16_t head[WAVE_BALL_CELLS],offset[WAVE_BALL_CELLS+1],cell_id[WAVE_BALL_COUNT];
    uint32_t random;
    unsigned stage,cursor,prefix;
    float dt,gx,gy,gz;
} wave_balls_t;
static inline wave_ball_view_t wave_balls_read(const wave_balls_t *s,unsigned i){
    const wave_ball_t *p=&s->p[i];
    return (wave_ball_view_t){p->x/128.0f,p->y/128.0f,p->z/128.0f,p->vx/32.0f,p->vy/32.0f,p->vz/32.0f};
}
void wave_balls_reset(wave_balls_t *s);
void wave_balls_reset_range(wave_balls_t *s,unsigned first,unsigned count);
void wave_balls_begin(wave_balls_t *s,float gx,float gy,float gz,float dt);
int wave_balls_work(wave_balls_t *s,unsigned batch);
void wave_balls_step(wave_balls_t *s,float gx,float gy,float gz,float dt);
void wave_balls_splash(wave_balls_t *s);
void wave_balls_splash_range(wave_balls_t *s,unsigned first,unsigned count);
