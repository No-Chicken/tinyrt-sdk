#include <assert.h>
#include <math.h>
#include "../../examples/wave_sim/wave_balls.h"
static wave_balls_t s;
static wave_balls_t sliced,whole;
int main(void){
    wave_balls_reset(&whole);
    for(unsigned i=0;i<WAVE_BALL_COUNT;i+=16)wave_balls_reset_range(&sliced,i,16);
    for(unsigned n=0;n<120;n++){
        wave_balls_begin(&sliced,.3f,-.7f,.2f,1.0f/60.0f);
        while(!wave_balls_work(&sliced,4)){}
        wave_balls_step(&whole,.3f,-.7f,.2f,1.0f/60.0f);
        for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
            assert(sliced.p[i].x==whole.p[i].x&&sliced.p[i].y==whole.p[i].y&&sliced.p[i].z==whole.p[i].z);
        }
    }
    wave_balls_reset(&s);
    for(unsigned n=0;n<3600;n++){
        float gx=(n/600)%2?0.7f:-0.7f,gy=(n/300)%2?0.7f:-0.7f;
        wave_balls_step(&s,gx,gy,0.2f,1.0f/60.0f);
        for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
            wave_ball_view_t p=wave_balls_read(&s,i);
            assert(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z));
            assert(p.x*p.x+p.y*p.y<=WAVE_BALL_BOUND*WAVE_BALL_BOUND+1);
            assert(p.z>=0&&p.z<=WAVE_BALL_DEPTH);
            assert(fabsf(p.vx)<=600&&fabsf(p.vy)<=600&&fabsf(p.vz)<=600);
        }
    }
    wave_balls_splash(&s);
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++)assert(isfinite(wave_balls_read(&s,i).vx));
    return 0;
}
