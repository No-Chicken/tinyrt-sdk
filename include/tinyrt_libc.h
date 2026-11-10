#ifndef TINYRT_LIBC_H
#define TINYRT_LIBC_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>

/* Freestanding helpers. On the host use the tr_ names alongside native libc. */
static inline void *tr_memcpy(void *dst,const void *src,size_t n)
{
    unsigned char *d=dst;
    const unsigned char *s=src;
    for(size_t i=0;i<n;i++) d[i]=s[i];
    return dst;
}
static inline void *tr_memset(void *dst,int c,size_t n)
{
    unsigned char *d=dst;
    for(size_t i=0;i<n;i++) d[i]=(unsigned char)c;
    return dst;
}
static inline void *tr_memmove(void *dst,const void *src,size_t n)
{
    unsigned char *d=dst;
    const unsigned char *s=src;
    if((uintptr_t)d<=(uintptr_t)s || (uintptr_t)d-(uintptr_t)s>=n)
        return tr_memcpy(dst,src,n);
    while(n) { --n; d[n]=s[n]; }
    return dst;
}
static inline int tr_memcmp(const void *a,const void *b,size_t n)
{
    const unsigned char *x=a,*y=b;
    for(size_t i=0;i<n;i++) if(x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
static inline size_t tr_strlen(const char *s)
{
    size_t n=0;
    while(s[n]) ++n;
    return n;
}
static inline int tr_strcmp(const char *a,const char *b)
{
    while(*a && *a==*b) { ++a; ++b; }
    return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
static inline void tr_format_char(char *dst,size_t capacity,size_t *count,char value)
{
    if(capacity && *count<capacity-1) dst[*count]=value;
    ++*count;
}
/* Returns the required count excluding NUL. Supports %d %u %x %s %c and %%. */
static inline int tr_snprintf(char *dst,size_t capacity,const char *format,...)
{
    size_t count=0;
    va_list args;
    va_start(args,format);
    while(*format) {
        char c=*format++;
        if(c!='%') { tr_format_char(dst,capacity,&count,c); continue; }
        c=*format++;
        if(c=='s') {
            const char *s=va_arg(args,const char *);
            while(*s) tr_format_char(dst,capacity,&count,*s++);
        } else if(c=='%' || c=='c') {
            tr_format_char(dst,capacity,&count,c=='%'?'%':(char)va_arg(args,int));
        } else if(c=='d' || c=='u' || c=='x') {
            unsigned int value,base=c=='x'?16u:10u;
            if(c=='d') {
                int v=va_arg(args,int);
                value=(unsigned)v;
                if(v<0) { tr_format_char(dst,capacity,&count,'-'); value=0u-value; }
            } else value=va_arg(args,unsigned int);
            char digits[sizeof(unsigned int)*CHAR_BIT];
            size_t used=0;
            do { digits[used++]="0123456789abcdef"[value%base]; value/=base; } while(value);
            while(used) tr_format_char(dst,capacity,&count,digits[--used]);
        } else {
            if(capacity) dst[count<capacity?count:capacity-1]=0;
            va_end(args);
            return -1;
        }
    }
    va_end(args);
    if(capacity) dst[count<capacity?count:capacity-1]=0;
    return count>INT_MAX?-1:(int)count;
}

typedef int32_t tr_q16_t;
#define TR_Q16_ONE INT32_C(65536)
static inline tr_q16_t tr_q16_sat(int64_t n)
{
    return n>INT32_MAX?INT32_MAX:n<INT32_MIN?INT32_MIN:(tr_q16_t)n;
}
static inline tr_q16_t tr_q16_mul(tr_q16_t a,tr_q16_t b)
{
    return tr_q16_sat((int64_t)a*b/TR_Q16_ONE);
}
static inline tr_q16_t tr_q16_div(tr_q16_t a,tr_q16_t b)
{
    if(!b) return a<0?INT32_MIN:INT32_MAX;
    return tr_q16_sat((int64_t)a*TR_Q16_ONE/b);
}
/* Phase wraps at 65536 units per full turn. Quarter-wave table plus interpolation. */
static inline tr_q16_t tr_sin_turn(uint16_t phase)
{
    static const int32_t quarter[65]={
        0,1608,3216,4821,6424,8022,9616,11204,
        12785,14359,15924,17479,19024,20557,22078,23586,
        25080,26558,28020,29466,30893,32303,33692,35062,
        36410,37736,39040,40320,41576,42806,44011,45190,
        46341,47464,48559,49624,50660,51665,52639,53581,
        54491,55368,56212,57022,57798,58538,59244,59914,
        60547,61145,61705,62228,62714,63162,63572,63944,
        64277,64571,64827,65043,65220,65358,65457,65516,
        65536
    };
    unsigned x=phase&16383u;
    if(phase&16384u) x=16384u-x;
    unsigned i=x/256u;
    int32_t value=quarter[i];
    if(i<64) value+=(quarter[i+1]-value)*(int32_t)(x%256u)/256;
    return (phase&32768u)?-value:value;
}
static inline tr_q16_t tr_cos_turn(uint16_t phase)
{
    return tr_sin_turn((uint16_t)(phase+16384u));
}
static inline uint32_t tr_xorshift32(uint32_t *state)
{
    uint32_t x=*state?*state:UINT32_C(0x6d2b79f5);
    x^=x<<13; x^=x>>17; x^=x<<5;
    return *state=x;
}

#if defined(__wasm__) && !defined(TINYRT_LIBC_PREFIX_ONLY)
#define memcpy tr_memcpy
#define memset tr_memset
#define memmove tr_memmove
#define memcmp tr_memcmp
#define strlen tr_strlen
#define strcmp tr_strcmp
#define snprintf tr_snprintf
#endif
#endif
