#include "wave_balls.h"

/* XY 十像素、Z 二十四像素网格；邻域覆盖十像素接触核。 */
static float root(float v){return __builtin_sqrtf(v);}
static float random_unit(wave_balls_t *s){
    uint32_t n=s->random;n^=n<<13;n^=n>>17;n^=n<<5;s->random=n;
    return (float)(n>>8)*(1.0f/16777216.0f);
}
static int cell_x(int x){int v=(x+24576)/1280;return v<0?0:v>=WAVE_BALL_GRID_X?WAVE_BALL_GRID_X-1:v;}
static int cell_z(int z){int v=z/3072;return v<0?0:v>=WAVE_BALL_GRID_Z?WAVE_BALL_GRID_Z-1:v;}
static int16_t velocity(float v){return (int16_t)(v>19200?19200:v< -19200?-19200:v);}
static int16_t coordinate(int32_t v){return (int16_t)(v>32000?32000:v< -32000?-32000:v);}
static int cell(int x,int y,int z){return (z*WAVE_BALL_GRID_X+y)*WAVE_BALL_GRID_X+x;}
void wave_balls_reset_range(wave_balls_t *s,unsigned first,unsigned count){
    if(first==0)s->random=0x72dc93a1u;
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        float x,y;
        do{x=(random_unit(s)*2-1)*180;y=(random_unit(s)*2-1)*180;}while(x*x+y*y>32400);
        s->p[i]=(wave_ball_t){(int16_t)(x*128),(int16_t)(y*128),(int16_t)(random_unit(s)*WAVE_BALL_DEPTH*128),0,0,0};
    }
}
void wave_balls_reset(wave_balls_t *s){wave_balls_reset_range(s,0,WAVE_BALL_COUNT);}
static void boundary(wave_ball_t *p){
    float d2=(float)((int32_t)p->x*p->x+(int32_t)p->y*p->y);
    if(d2>WAVE_BALL_BOUND*WAVE_BALL_BOUND*16384){
        float inv=1/root(d2),nx=p->x*inv,ny=p->y*inv;
        p->x=(int16_t)(nx*WAVE_BALL_BOUND*128);p->y=(int16_t)(ny*WAVE_BALL_BOUND*128);
        float out=p->vx*nx+p->vy*ny;
        if(out>0){p->vx=velocity(p->vx-1.35f*out*nx);p->vy=velocity(p->vy-1.35f*out*ny);}
    }
    if(p->z<0){p->z=0;if(p->vz<0)p->vz=(int16_t)(p->vz*-.35f);}
    if(p->z>WAVE_BALL_DEPTH*128){p->z=(int16_t)(WAVE_BALL_DEPTH*128);if(p->vz>0)p->vz=(int16_t)(p->vz*-.35f);}
}
void wave_balls_begin(wave_balls_t *s,float gx,float gy,float gz,float dt){
    s->stage=0;s->cursor=s->prefix=0;s->gx=gx;s->gy=gy;s->gz=gz;
    s->dt=dt>.05f?.05f:dt;
    if(!(dt>0))s->stage=8;
}
int wave_balls_work(wave_balls_t *s,unsigned batch){
    if(!batch)return s->stage==8;
    float dt=s->dt,gx=s->gx,gy=s->gy,gz=s->gz;
    unsigned end=s->cursor+batch;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    if(s->stage==0){
    float drag=1.0f-0.8f*dt;
    for(unsigned i=s->cursor;i<end;i++){
        wave_ball_t *p=&s->p[i];
        p->vx=velocity((p->vx+gx*11520*dt)*drag);p->vy=velocity((p->vy+gy*11520*dt)*drag);
        p->vz=velocity((p->vz+gz*11520*dt)*drag);
        p->x=(int16_t)(p->x+p->vx*dt*4);p->y=(int16_t)(p->y+p->vy*dt*4);p->z=(int16_t)(p->z+p->vz*dt*4);boundary(p);
    }
    }else if(s->stage==1){
        end=s->cursor+256;if(end>WAVE_BALL_CELLS)end=WAVE_BALL_CELLS;
        for(unsigned c=s->cursor;c<end;c++)s->head[c]=0;
        s->cursor=end;if(end==WAVE_BALL_CELLS){s->stage=2;s->cursor=0;}return 0;
    }else if(s->stage==2){
    for(unsigned i=s->cursor;i<end;i++){
        wave_ball_t *p=&s->p[i];int c=cell(cell_x(p->x),cell_x(p->y),cell_z(p->z));
        s->cell_id[i]=(uint16_t)c;s->head[c]++;
    }
    }else if(s->stage==3){
        end=s->cursor+256;if(end>WAVE_BALL_CELLS)end=WAVE_BALL_CELLS;
        for(unsigned c=s->cursor;c<end;c++){
            unsigned count=s->head[c];s->offset[c]=(uint16_t)s->prefix;
            s->head[c]=(uint16_t)s->prefix;s->prefix+=count;
        }
        s->cursor=end;if(end==WAVE_BALL_CELLS){s->offset[end]=(uint16_t)s->prefix;s->stage=4;s->cursor=0;}return 0;
    }else if(s->stage==4){
        for(unsigned i=s->cursor;i<end;i++)s->sorted[s->head[s->cell_id[i]]++]=s->p[i];
    }else if(s->stage==5){
        for(unsigned i=s->cursor;i<end;i++)s->p[i]=s->sorted[i];
    }else if(s->stage==6){
    for(unsigned i=s->cursor;i<end;i++){
        wave_ball_t *a=&s->p[i];int cx=cell_x(a->x),cy=cell_x(a->y),cz=cell_z(a->z);
        int x0=cx>0?cx-1:0,x1=cx<WAVE_BALL_GRID_X-1?cx+1:WAVE_BALL_GRID_X-1;
        int y0=cy>0?cy-1:0,y1=cy<WAVE_BALL_GRID_X-1?cy+1:WAVE_BALL_GRID_X-1;
        int z0=cz>0?cz-1:0,z1=cz<WAVE_BALL_GRID_Z-1?cz+1:WAVE_BALL_GRID_Z-1;
        for(int z=z0;z<=z1;z++)for(int y=y0;y<=y1;y++){
            unsigned begin=s->offset[cell(x0,y,z)],stop=s->offset[cell(x1,y,z)+1];
            if(begin<=i)begin=i+1;
            for(unsigned j=begin;j<stop;j++){
                wave_ball_t *b=&s->p[j];
                int32_t dx=(int32_t)b->x-a->x,dy=(int32_t)b->y-a->y,dz=(int32_t)b->z-a->z;
                const int contact=WAVE_BALL_CONTACT_Q7;
                if(dx<=-contact||dx>=contact||dy<=-contact||dy>=contact||dz<=-contact||dz>=contact)continue;
                uint32_t d2=(uint32_t)(dx*dx+dy*dy+dz*dz);
                if(d2>=(uint32_t)(contact*contact)||d2==0)continue;
                float d=root((float)d2),inv=1/d,push=(contact-d)*.5f;
                float nx=dx*inv,ny=dy*inv,nz=dz*inv;
                int32_t px=(int32_t)(nx*push),py=(int32_t)(ny*push),pz=(int32_t)(nz*push);
                a->x=coordinate(a->x-px);a->y=coordinate(a->y-py);a->z=coordinate(a->z-pz);
                b->x=coordinate(b->x+px);b->y=coordinate(b->y+py);b->z=coordinate(b->z+pz);
                float approach=(b->vx-a->vx)*nx+(b->vy-a->vy)*ny+(b->vz-a->vz)*nz;
                if(approach<0){float impulse=approach*.55f;
                    a->vx=velocity(a->vx+nx*impulse);a->vy=velocity(a->vy+ny*impulse);a->vz=velocity(a->vz+nz*impulse);
                    b->vx=velocity(b->vx-nx*impulse);b->vy=velocity(b->vy-ny*impulse);b->vz=velocity(b->vz-nz*impulse);}
            }
        }
    }
    }else if(s->stage==7){
        for(unsigned i=s->cursor;i<end;i++)boundary(&s->p[i]);
    }else return 1;
    s->cursor=end;if(end==WAVE_BALL_COUNT){s->stage++;s->cursor=0;}
    return s->stage==8;
}
void wave_balls_step(wave_balls_t *s,float gx,float gy,float gz,float dt){
    wave_balls_begin(s,gx,gy,gz,dt);while(!wave_balls_work(s,WAVE_BALL_COUNT)){}
}
void wave_balls_splash_range(wave_balls_t *s,unsigned first,unsigned count){
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        wave_ball_t *p=&s->p[i];p->vx=velocity(p->vx+(random_unit(s)*2-1)*5120);
        p->vy=velocity(p->vy+(random_unit(s)*2-1)*5120);p->vz=velocity(p->vz+(random_unit(s)*2-1)*5120);
    }
}
void wave_balls_splash(wave_balls_t *s){wave_balls_splash_range(s,0,WAVE_BALL_COUNT);}
