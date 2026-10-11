#include "wave_balls.h"

/* XYZ 二十四像素核；分两遍估计密度与松弛压力。 */
static float root(float v){return __builtin_sqrtf(v);}
static float random_unit(wave_balls_t *s){
    uint32_t n=s->random;n^=n<<13;n^=n>>17;n^=n<<5;s->random=n;
    return (float)(n>>8)*(1.0f/16777216.0f);
}
static int cell_x(int x){int v=(x+32768)/3072;return v<0?0:v>=WAVE_BALL_GRID_X?WAVE_BALL_GRID_X-1:v;}
static int cell_z(int z){int v=z/3072;return v<0?0:v>=WAVE_BALL_GRID_Z?WAVE_BALL_GRID_Z-1:v;}
static int16_t velocity(float v){return (int16_t)(v>19200?19200:v< -19200?-19200:v);}
static int16_t coordinate(int32_t v){return (int16_t)(v>32700?32700:v< -32700?-32700:v);}
static int cell(int x,int y,int z){return (z*WAVE_BALL_GRID_X+y)*WAVE_BALL_GRID_X+x;}
void wave_balls_reset_range(wave_balls_t *s,unsigned first,unsigned count){
    if(first==0)s->random=0x72dc93a1u;
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        float x,y;
        float z=random_unit(s)*WAVE_BALL_DEPTH,r=wave_balls_radius(z)-8;
        do{x=(random_unit(s)*2-1)*r;y=(random_unit(s)*2-1)*r;}while(x*x+y*y>r*r);
        s->p[i]=(wave_ball_t){(int16_t)(x*128),(int16_t)(y*128),(int16_t)(z*128),0,0,0,(uint16_t)i};
    }
}
void wave_balls_reset(wave_balls_t *s){wave_balls_reset_range(s,0,WAVE_BALL_COUNT);}
static void boundary(wave_ball_t *p){
    if(p->z<0){p->z=0;if(p->vz<0)p->vz=(int16_t)(p->vz*-.35f);}
    if(p->z>WAVE_BALL_DEPTH*128){p->z=(int16_t)(WAVE_BALL_DEPTH*128);if(p->vz>0)p->vz=(int16_t)(p->vz*-.35f);}
    /* 透视侧壁随深度展开，投影后仍贴合整个圆屏，而非中央小圆。 */
    float radius=wave_balls_radius(p->z/128.0f);
    float d2=(float)((int32_t)p->x*p->x+(int32_t)p->y*p->y);
    if(d2>radius*radius*16384){
        float inv=1/root(d2),nx=p->x*inv,ny=p->y*inv;
        p->x=(int16_t)(nx*radius*128);p->y=(int16_t)(ny*radius*128);
        float out=p->vx*nx+p->vy*ny;
        if(out>0){p->vx=velocity(p->vx-1.35f*out*nx);p->vy=velocity(p->vy-1.35f*out*ny);}
    }
}
static void relax_neighbor(wave_balls_t *s,unsigned i,unsigned j,float dt){
    wave_ball_t *a=&s->p[i],*b=&s->p[j];
    float dx=(float)b->x-a->x,dy=(float)b->y-a->y,dz=(float)b->z-a->z;
    float d=root(dx*dx+dy*dy+dz*dz),q=1.f-d/WAVE_BALL_CONTACT_Q7,q2=q*q;
                    /* Double-density relaxation with bounded Jacobi corrections. */
                    float pi=(s->density[i]-.51f)*1.5f,pj=(s->density[j]-.51f)*1.5f;
                    if(pi<-.08f)pi=-.08f;if(pj<-.08f)pj=-.08f;
                    float push=.5f*((pi+pj)*q+2.0f*(s->near_density[i]+s->near_density[j])*q2);
                    if(push>.6f)push=.6f;
                    float nx=dx/d,ny=dy/d,nz=dz/d;
                    float shift[3]={nx*push,ny*push,nz*push};
                    float va[3]={a->vx/32.f,a->vy/32.f,a->vz/32.f};
                    float vb[3]={b->vx/32.f,b->vy/32.f,b->vz/32.f};
                    for(unsigned axis=0;axis<3;axis++){
                        float visc=(vb[axis]-va[axis])*.025f*q*dt;
                        s->move[i][axis]+=visc-shift[axis];
                        s->move[j][axis]-=visc-shift[axis];
                    }
}
void wave_balls_begin(wave_balls_t *s,float gx,float gy,float gz,float dt){
    s->stage=0;s->cursor=s->prefix=s->pair_active=0;s->gx=gx;s->gy=gy;s->gz=gz;
    if(!(dt>0)){s->stage=9;s->substeps=0;return;}
    if(dt>.05f)dt=.05f;
    s->substeps=(unsigned)(dt*60.f+.999f);if(!s->substeps)s->substeps=1;
    s->dt=dt/s->substeps;
}
int wave_balls_work(wave_balls_t *s,unsigned batch){
    if(!batch)return s->stage==9;
    float dt=s->dt,gx=s->gx,gy=s->gy,gz=s->gz;
    unsigned end=s->cursor+batch;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    if(s->stage==0){
    if(!s->cursor){s->pair_count=0;s->pair_cached=1;}
    float drag=1.0f-0.8f*dt;
    for(unsigned i=s->cursor;i<end;i++){
        wave_ball_t *p=&s->p[i];
        s->old[p->id][0]=p->x;s->old[p->id][1]=p->y;s->old[p->id][2]=p->z;
        p->vx=velocity((p->vx+gx*11520*dt)*drag);p->vy=velocity((p->vy+gy*11520*dt)*drag);
        p->vz=velocity((p->vz+gz*11520*dt)*drag);
        p->x=coordinate((int32_t)(p->x+p->vx*dt*4));p->y=coordinate((int32_t)(p->y+p->vy*dt*4));p->z=coordinate((int32_t)(p->z+p->vz*dt*4));boundary(p);
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
        for(unsigned i=s->cursor;i<end;i++){s->p[i]=s->sorted[i];s->density[i]=s->near_density[i]=0;s->move[i][0]=s->move[i][1]=s->move[i][2]=0;}
    }else if(s->stage==6||s->stage==7){
        unsigned pairs=batch*8;
        if(s->stage==7&&s->pair_cached){
            while(s->cursor<end&&pairs){
                unsigned i=s->cursor;
                if(!s->pair_active){s->pair_next=s->pair_offset[i];s->pair_active=1;}
                unsigned stop=s->pair_offset[i+1];
                while(s->pair_next<stop&&pairs){relax_neighbor(s,i,s->neighbors[s->pair_next++],dt);pairs--;}
                if(s->pair_next==stop){s->pair_active=0;s->cursor++;}
            }
            if(s->cursor==WAVE_BALL_COUNT){s->stage=8;s->cursor=s->pair_active=0;}
            return 0;
        }
        while(s->cursor<end&&pairs){
            unsigned i=s->cursor;wave_ball_t *a=&s->p[i];
            if(!s->pair_active){
                if(s->stage==6)s->pair_offset[i]=(uint16_t)s->pair_count;
                int cx=cell_x(a->x),cy=cell_x(a->y),cz=cell_z(a->z);
                s->pair_x0=cx>0?cx-1:0;s->pair_x1=cx<WAVE_BALL_GRID_X-1?cx+1:WAVE_BALL_GRID_X-1;
                s->pair_y0=cy>0?cy-1:0;s->pair_y1=cy<WAVE_BALL_GRID_X-1?cy+1:WAVE_BALL_GRID_X-1;
                s->pair_z0=cz>0?cz-1:0;s->pair_z1=cz<WAVE_BALL_GRID_Z-1?cz+1:WAVE_BALL_GRID_Z-1;
                s->pair_row=s->pair_next=0;s->pair_active=1;
            }
            unsigned rows=(unsigned)((s->pair_y1-s->pair_y0+1)*(s->pair_z1-s->pair_z0+1));
            while(s->pair_row<rows&&pairs){
                int y=s->pair_y0+(int)(s->pair_row%(unsigned)(s->pair_y1-s->pair_y0+1));
                int z=s->pair_z0+(int)(s->pair_row/(unsigned)(s->pair_y1-s->pair_y0+1));
                unsigned begin=s->offset[cell(s->pair_x0,y,z)],stop=s->offset[cell(s->pair_x1,y,z)+1];
                if(begin<=i)begin=i+1;
                if(s->pair_next<begin)s->pair_next=begin;
                if(s->pair_next>=stop){s->pair_row++;s->pair_next=0;continue;}
                while(s->pair_next<stop&&pairs){
                unsigned j=s->pair_next++;pairs--;
                wave_ball_t *b=&s->p[j];
                int32_t dx=(int32_t)b->x-a->x,dy=(int32_t)b->y-a->y,dz=(int32_t)b->z-a->z;
                const int contact=WAVE_BALL_CONTACT_Q7;
                if(dx<=-contact||dx>=contact||dy<=-contact||dy>=contact||dz<=-contact||dz>=contact)continue;
                uint32_t d2=(uint32_t)(dx*dx+dy*dy+dz*dz);
                if(d2>=(uint32_t)(contact*contact)||d2==0)continue;
                float d=root((float)d2),q=1.0f-d/(float)contact,q2=q*q;
                if(s->stage==6){
                    if(s->pair_count<WAVE_BALL_COUNT*12)s->neighbors[s->pair_count++]=(uint16_t)j;
                    else s->pair_cached=0;
                    s->density[i]+=q2;s->density[j]+=q2;
                    s->near_density[i]+=q2*q;s->near_density[j]+=q2*q;
                }else {
                    relax_neighbor(s,i,j,dt);
                }
                }
                if(s->pair_next>=stop){s->pair_row++;s->pair_next=0;}
            }
            if(s->pair_row==rows){if(s->stage==6)s->pair_offset[i+1]=(uint16_t)s->pair_count;s->pair_active=0;s->cursor++;}
        }
        if(s->cursor==WAVE_BALL_COUNT){s->stage++;s->cursor=s->pair_active=0;}
        return 0;
    }else if(s->stage==8){
        for(unsigned i=s->cursor;i<end;i++){
            wave_ball_t *p=&s->p[i];float *m=s->move[i];
            float len=root(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);
            float scale=len>1.5f?1.5f/len:1.f;
            p->x=coordinate(p->x+(int32_t)(m[0]*scale*128));
            p->y=coordinate(p->y+(int32_t)(m[1]*scale*128));
            p->z=coordinate(p->z+(int32_t)(m[2]*scale*128));boundary(p);
            float vx=(p->x-s->old[p->id][0])*(.25f/dt)*.97f;
            float vy=(p->y-s->old[p->id][1])*(.25f/dt)*.97f;
            float vz=(p->z-s->old[p->id][2])*(.25f/dt)*.97f;
            if(vx>-1.2f&&vx<1.2f)vx=0;if(vy>-1.2f&&vy<1.2f)vy=0;if(vz>-1.2f&&vz<1.2f)vz=0;
            p->vx=velocity(vx);p->vy=velocity(vy);p->vz=velocity(vz);
        }
    }else return 1;
    s->cursor=end;if(end==WAVE_BALL_COUNT){s->stage++;s->cursor=0;}
    if(s->stage==9&&s->substeps>1){
        s->substeps--;s->stage=0;s->cursor=s->prefix=s->pair_active=0;
    }
    return s->stage==9;
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
