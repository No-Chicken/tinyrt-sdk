#include "wave_physics.h"

/* Wasm f32.sqrt，由 AOT 后端执行；避免每次求根做四次浮点除法。 */
float wave_sqrt(float value) {
    if(value<=0.0f)return 0.0f;
    return __builtin_sqrtf(value);
}
/* 输入 Q24 平方距离，返回 Q12 距离。整数逐位平方根不调用浮点库。 */
static uint32_t distance_q12(uint32_t n) {
    uint32_t root=0,bit=1u<<((31u-(unsigned)__builtin_clz(n))&~1u);
    while(bit){
        if(n>=root+bit){n-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return root;
}
void wave_separate(int32_t *ax,int32_t *ay,int32_t *bx,int32_t *by) {
    int32_t dx=*bx-*ax,dy=*by-*ay;
    if(dx<=-65536||dx>=65536||dy<=-65536||dy>=65536)return;
    int32_t ux=(dx<0?-dx:dx)>>4,uy=(dy<0?-dy:dy)>>4;
    uint32_t d2=(uint32_t)(ux*ux+uy*uy);
    /* Q12 取样约 1/4096 格，沿用距离平方 (1e-6,1) 的约束范围。 */
    if(d2<17u||d2>=16777216u)return;
    int32_t d=(int32_t)distance_q12(d2);
    int32_t px=dx*(4096-d)/(2*d),py=dy*(4096-d)/(2*d);
    *ax-=px;*ay-=py;*bx+=px;*by+=py;
}
static float random_unit(wave_state_t *s) {
    uint32_t n=s->random;n^=n<<13;n^=n>>17;n^=n<<5;s->random=n;
    return (float)(n>>8)*(1.0f/16777216.0f);
}
void wave_reset(wave_state_t *s) {
    s->gx=s->tx=0;s->gy=s->ty=1;s->random=0x57a9c103u;
    int i=0;
    for(float y=WAVE_GRID-1;y>0&&i<WAVE_N;y-=0.95f)for(float x=0;x<WAVE_GRID&&i<WAVE_N;x+=0.95f) {
        float dx=x-WAVE_CENTER,dy=y-WAVE_CENTER;
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
    float ax=s->gx*(38.0f*WAVE_RATIO)*dt*dt,ay=s->gy*(38.0f*WAVE_RATIO)*dt*dt;
    for(int i=0;i<WAVE_N;i++) {
        float vx=(s->px[i]-s->ox[i])*0.995f,vy=(s->py[i]-s->oy[i])*0.995f;
        s->ox[i]=s->px[i];s->oy[i]=s->py[i];s->px[i]+=vx+ax;s->py[i]+=vy+ay;
    }
        s->stage=1;return 0;
    }
    if(s->stage==1) {
        for(int c=0;c<WAVE_CELLS;c++)s->head[c]=-1;
        for(int i=0;i<WAVE_N;i++) {
            s->qx[i]=(int32_t)(s->px[i]*65536.0f);s->qy[i]=(int32_t)(s->py[i]*65536.0f);
            int x=s->qx[i]/65536,y=s->qy[i]/65536;
            if(x>=0&&x<WAVE_GRID&&y>=0&&y<WAVE_GRID){int c=y*WAVE_GRID+x;s->next[i]=s->head[c];s->head[c]=(int16_t)i;}
            else s->next[i]=-1;
        }
        s->stage=2;s->cursor=0;return 0;
    }
    if(s->stage==2) {
        int end=s->cursor+batch;if(end>WAVE_N)end=WAVE_N;
        for(int i=s->cursor;i<end;i++) {
            int cx=s->qx[i]/65536,cy=s->qy[i]/65536;
            for(int y=cy-1;y<=cy+1;y++)for(int x=cx-1;x<=cx+1;x++) {
                if(x<0||x>=WAVE_GRID||y<0||y>=WAVE_GRID)continue;
                for(int j=s->head[y*WAVE_GRID+x];j>=0;j=s->next[j]) {
                    if(j<=i)continue;
                    wave_separate(&s->qx[i],&s->qy[i],&s->qx[j],&s->qy[j]);
                }
            }
        }
        s->cursor=end;if(end==WAVE_N)s->stage=3;return 0;
    }
    if(s->stage==3) {
        for(int i=0;i<WAVE_N;i++) {
            s->px[i]=(float)s->qx[i]*(1.0f/65536.0f);s->py[i]=(float)s->qy[i]*(1.0f/65536.0f);
            float dx=s->px[i]-WAVE_CENTER,dy=s->py[i]-WAVE_CENTER,d2=dx*dx+dy*dy;
            if(d2>WAVE_R*WAVE_R) {
                float d=wave_sqrt(d2),nx=dx/d,ny=dy/d;
                s->px[i]=WAVE_CENTER+nx*WAVE_R;s->py[i]=WAVE_CENTER+ny*WAVE_R;
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
        float push=(0.6f+random_unit(s)*0.9f)*WAVE_RATIO;
        s->ox[i]=s->px[i]+s->gx*push+(random_unit(s)-0.5f)*(0.6f*WAVE_RATIO);
        s->oy[i]=s->py[i]+s->gy*push+(random_unit(s)-0.5f)*(0.6f*WAVE_RATIO);
    }
}
