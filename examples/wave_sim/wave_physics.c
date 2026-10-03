#include "wave_physics.h"

/* IEEE float 初值后做 Newton 迭代，无 libc 或未声明的 Wasm import。 */
float wave_sqrt(float value) {
    if(value<=0.0f)return 0.0f;
    union { float f;uint32_t u; } estimate={.f=value};
    estimate.u=(estimate.u>>1)+0x1fc00000u;
    float x=estimate.f;
    for(int i=0;i<4;i++)x=0.5f*(x+value/x);
    return x;
}
static float random_unit(wave_state_t *s) {
    uint32_t n=s->random;n^=n<<13;n^=n>>17;n^=n<<5;s->random=n;
    return (float)(n>>8)*(1.0f/16777216.0f);
}
void wave_reset(wave_state_t *s) {
    s->gx=s->tx=0;s->gy=s->ty=1;s->random=0x57a9c103u;
    int i=0;
    for(float y=39;y>0&&i<WAVE_N;y-=0.95f)for(float x=0;x<40&&i<WAVE_N;x+=0.95f) {
        float dx=x-20,dy=y-20;
        if(dx*dx+dy*dy<(WAVE_R-0.5f)*(WAVE_R-0.5f)) {
            s->px[i]=s->ox[i]=x;s->py[i]=s->oy[i]=y;s->speed[i]=0;i++;
        }
    }
}
void wave_tilt(wave_state_t *s,int32_t x,int32_t y) {
    float fx=(float)x,fy=(float)y,m=wave_sqrt(fx*fx+fy*fy);
    if(m>=150.0f){s->tx=fx/m;s->ty=fy/m;}
}
void wave_begin(wave_state_t *s,float dt) {
    s->stage=0;s->iteration=0;s->cursor=0;s->dt=dt;
}
int wave_work(wave_state_t *s,int batch) {
    float dt=s->dt;
    if(s->stage==0) {
    s->gx+=(s->tx-s->gx)*0.2f;s->gy+=(s->ty-s->gy)*0.2f;
    float ax=s->gx*38.0f*dt*dt,ay=s->gy*38.0f*dt*dt;
    for(int i=0;i<WAVE_N;i++) {
        float vx=(s->px[i]-s->ox[i])*0.995f,vy=(s->py[i]-s->oy[i])*0.995f;
        s->ox[i]=s->px[i];s->oy[i]=s->py[i];s->px[i]+=vx+ax;s->py[i]+=vy+ay;
    }
        s->stage=1;return 0;
    }
    if(s->stage==1) {
        for(int c=0;c<1600;c++)s->head[c]=-1;
        for(int i=0;i<WAVE_N;i++) {
            int x=(int)s->px[i],y=(int)s->py[i];
            if(x>=0&&x<40&&y>=0&&y<40){int c=y*40+x;s->next[i]=s->head[c];s->head[c]=(int16_t)i;}
            else s->next[i]=-1;
        }
        s->stage=2;s->cursor=0;return 0;
    }
    if(s->stage==2) {
        int end=s->cursor+batch;if(end>WAVE_N)end=WAVE_N;
        for(int i=s->cursor;i<end;i++) {
            int cx=(int)s->px[i],cy=(int)s->py[i];
            for(int y=cy-1;y<=cy+1;y++)for(int x=cx-1;x<=cx+1;x++) {
                if(x<0||x>=40||y<0||y>=40)continue;
                for(int j=s->head[y*40+x];j>=0;j=s->next[j]) {
                    if(j<=i)continue;
                    float dx=s->px[j]-s->px[i],dy=s->py[j]-s->py[i],d2=dx*dx+dy*dy;
                    if(d2<1.0f&&d2>0.000001f) {
                        float d=wave_sqrt(d2),k=(1.0f-d)*0.5f/d;
                        dx*=k;dy*=k;s->px[i]-=dx;s->py[i]-=dy;s->px[j]+=dx;s->py[j]+=dy;
                    }
                }
            }
        }
        s->cursor=end;if(end==WAVE_N)s->stage=3;return 0;
    }
    if(s->stage==3) {
        for(int i=0;i<WAVE_N;i++) {
            float dx=s->px[i]-20,dy=s->py[i]-20,d2=dx*dx+dy*dy;
            if(d2>WAVE_R*WAVE_R) {
                float d=wave_sqrt(d2),nx=dx/d,ny=dy/d;
                s->px[i]=20+nx*WAVE_R;s->py[i]=20+ny*WAVE_R;
                float vn=(s->px[i]-s->ox[i])*nx+(s->py[i]-s->oy[i])*ny;
                if(vn>0){s->ox[i]+=vn*nx*1.276f;s->oy[i]+=vn*ny*1.276f;}
            }
        }
        s->iteration++;s->stage=s->iteration<2?1:4;return 0;
    }
    for(int i=0;i<WAVE_N;i++){float dx=s->px[i]-s->ox[i],dy=s->py[i]-s->oy[i];s->speed[i]=wave_sqrt(dx*dx+dy*dy);}
    return 1;
}
void wave_step(wave_state_t *s,float dt) {
    wave_begin(s,dt);while(!wave_work(s,WAVE_N)){}
}
void wave_splash(wave_state_t *s) {
    for(int i=0;i<WAVE_N;i++) {
        float push=0.6f+random_unit(s)*0.9f;
        s->ox[i]=s->px[i]+s->gx*push+(random_unit(s)-0.5f)*0.6f;
        s->oy[i]=s->py[i]+s->gy*push+(random_unit(s)-0.5f)*0.6f;
    }
}
