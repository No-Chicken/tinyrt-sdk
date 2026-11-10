#include "tinyrt_libc.h"
#include <assert.h>

int main(void)
{
    char buf[64]="abcdef";
    assert(tr_memmove(buf+1,buf,5)==buf+1 && tr_strcmp(buf,"aabcde")==0);
    tr_memmove(buf,buf+1,5);
    assert(tr_memcmp(buf,"abcde",5)==0);
    tr_memset(buf,'Z',3);
    assert(tr_memcmp(buf,"ZZZde",5)==0);
    int n=tr_snprintf(buf,sizeof(buf),"%d %u %x %s %c %%",INT_MIN,UINT_MAX,0xabcu,"ok",'!');
    assert(n==(int)tr_strlen(buf));
    assert(tr_strcmp(buf,"-2147483648 4294967295 abc ok ! %")==0);
    assert(tr_snprintf(buf,3,"%s","abcd")==4 && tr_strcmp(buf,"ab")==0);
    assert(tr_snprintf(NULL,0,"%d",123)==3);
    assert(tr_q16_mul(INT32_MAX,INT32_MAX)==INT32_MAX);
    assert(tr_q16_div(-TR_Q16_ONE,2*TR_Q16_ONE)==-32768);
    assert(tr_sin_turn(0)==0 && tr_sin_turn(16384)==65536 && tr_cos_turn(32768)==-65536);
    uint32_t seed=0;
    assert(tr_xorshift32(&seed)!=0);
    return 0;
}
