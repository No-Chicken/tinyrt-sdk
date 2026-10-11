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
            assert(sliced.p[i].id==whole.p[i].id);
            assert(sliced.p[i].x==whole.p[i].x&&sliced.p[i].y==whole.p[i].y&&sliced.p[i].z==whole.p[i].z);
        }
    }
    /* One 30 Hz update is exactly two stable 60 Hz substeps. */
    wave_balls_reset(&whole);wave_balls_reset(&sliced);
    wave_balls_step(&whole,.3f,-.7f,.2f,1.f/30.f);
    wave_balls_step(&sliced,.3f,-.7f,.2f,1.f/60.f);
    wave_balls_step(&sliced,.3f,-.7f,.2f,1.f/60.f);
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
        assert(whole.p[i].id==sliced.p[i].id);
        assert(whole.p[i].x==sliced.p[i].x&&whole.p[i].y==sliced.p[i].y&&whole.p[i].z==sliced.p[i].z);
        assert(whole.p[i].vx==sliced.p[i].vx&&whole.p[i].vy==sliced.p[i].vy&&whole.p[i].vz==sliced.p[i].vz);
    }
    /* Cached neighbors and a full pressure scan must produce the same state. */
    wave_balls_reset(&whole);
    wave_balls_begin(&whole,.3f,-.7f,.2f,1.f/60.f);
    while(whole.stage<7)wave_balls_work(&whole,4);
    assert(whole.pair_cached);sliced=whole;sliced.pair_cached=0;
    while(!wave_balls_work(&whole,4)){}
    while(!wave_balls_work(&sliced,4)){}
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
        assert(whole.p[i].id==sliced.p[i].id);
        assert(whole.p[i].x==sliced.p[i].x&&whole.p[i].y==sliced.p[i].y&&whole.p[i].z==sliced.p[i].z);
        assert(whole.p[i].vx==sliced.p[i].vx&&whole.p[i].vy==sliced.p[i].vy&&whole.p[i].vz==sliced.p[i].vz);
    }
    /* A dense cell must yield while scanning the first particle's neighbors. */
    wave_balls_reset(&sliced);
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){sliced.p[i].x=(int16_t)(i%10);sliced.p[i].y=0;sliced.p[i].z=100;}
    wave_balls_begin(&sliced,0,0,0,1.f/60.f);
    while(sliced.stage<6)wave_balls_work(&sliced,4);
    wave_balls_work(&sliced,1);
    assert(sliced.stage==6&&sliced.cursor==0&&sliced.pair_next<=9);
    while(sliced.stage<7)wave_balls_work(&sliced,4);
    assert(!sliced.pair_cached&&sliced.pair_count==WAVE_BALL_COUNT*12);
    while(!wave_balls_work(&sliced,4)){}
    wave_balls_reset(&s);
    for(unsigned n=0;n<3600;n++){
        float gx=(n/600)%2?0.7f:-0.7f,gy=(n/300)%2?0.7f:-0.7f;
        wave_balls_step(&s,gx,gy,0.2f,1.0f/60.0f);
        unsigned char ids[WAVE_BALL_COUNT]={0};
        for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
            assert(s.p[i].id<WAVE_BALL_COUNT&&!ids[s.p[i].id]);ids[s.p[i].id]=1;
            wave_ball_view_t p=wave_balls_read(&s,i);
            assert(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z));
            float radius=wave_balls_radius(p.z);
            assert(p.x*p.x+p.y*p.y<=radius*radius+1);
            assert(p.z>=0&&p.z<=WAVE_BALL_DEPTH);
            assert(fabsf(p.vx)<=600&&fabsf(p.vy)<=600&&fabsf(p.vz)<=600);
        }
    }
    /* A dense pile must settle instead of retaining falling velocity forever. */
    wave_balls_reset(&s);
    static int16_t previous[WAVE_BALL_COUNT][3];double residual=0;unsigned samples=0;
    for(unsigned n=0;n<2400;n++){
        wave_balls_step(&s,.7f,0,.7f,1.0f/30.0f);
        for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
            wave_ball_t *p=&s.p[i];unsigned id=p->id;
            if(n>1800){
                double dx=(p->x-previous[id][0])/128.0,dy=(p->y-previous[id][1])/128.0,dz=(p->z-previous[id][2])/128.0;
                residual+=dx*dx+dy*dy+dz*dz;samples++;
            }
            previous[id][0]=p->x;previous[id][1]=p->y;previous[id][2]=p->z;
        }
    }
    assert(sqrt(residual/samples)<.5);
    wave_balls_splash(&s);
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++)assert(isfinite(wave_balls_read(&s,i).vx));
    return 0;
}
