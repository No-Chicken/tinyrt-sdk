/* Freestanding, deterministic memory helpers. N1 never allocates dynamically. */
#include "nes.h"
void *nes_malloc(int n) {(void)n;return NULL;}
void nes_free(void *p) {(void)p;}
void *nes_memcpy(void *d,const void *s,size_t n) {
    unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;
}
void *nes_memset(void *d,int c,size_t n) {
    unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;
}
int nes_memcmp(const void *a,const void *b,size_t n) {
    const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;
}
int nes_initex(nes_t *n) {(void)n;return 0;}
int nes_deinitex(nes_t *n) {(void)n;return 0;}
int nes_draw(int a,int b,int c,int d,nes_color_t *p) {(void)a;(void)b;(void)c;(void)d;(void)p;return 0;}
void nes_frame(nes_t *n) {(void)n;}
