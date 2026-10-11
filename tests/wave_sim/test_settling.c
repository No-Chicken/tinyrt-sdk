#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../examples/wave_sim/wave_physics.h"
#define WAVE_STEP_HZ 60u
#define WAVE_STEP_DT (2.0f/(float)WAVE_STEP_HZ)
/* 捕获约束不足造成的持续翻腾；统计 30 秒结束时实际位移，不使用修正后的 ox/oy。 */
static void test_settles(void) {
    static wave_state_t s;
    float previous_x[WAVE_N],previous_y[WAVE_N];
    for(int sliced=0;sliced<2;sliced++) {
        wave_reset(&s);
        double total=0;float maximum=0;
        for(unsigned step=0;step<30*WAVE_STEP_HZ;step++) {
            for(int i=0;i<WAVE_N;i++){previous_x[i]=s.px[i];previous_y[i]=s.py[i];}
            if(sliced){wave_begin(&s,WAVE_STEP_DT);while(!wave_work(&s,16)){};}
            else wave_step(&s,WAVE_STEP_DT);
            if(step==30*WAVE_STEP_HZ-1)for(int i=0;i<WAVE_N;i++) {
                float dx=s.px[i]-previous_x[i],dy=s.py[i]-previous_y[i];
                float distance=sqrtf(dx*dx+dy*dy);
                assert(isfinite(distance));
                total+=distance;if(distance>maximum)maximum=distance;
            }
        }
        double mean=total/WAVE_N;
        fprintf(stderr,"settling %dx%d/%d %s: mean=%.7f max=%.7f cells/step\n",
                WAVE_GRID,WAVE_GRID,WAVE_N,sliced?"sliced":"whole",mean,maximum);
        assert(mean<0.002&&maximum<0.01f);
    }
}
int main(void) { test_settles(); return 0; }
