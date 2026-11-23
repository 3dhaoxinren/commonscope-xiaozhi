#include "bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){CsBridge b={0};char out[36];uint32_t s,m;int n=0;
 assert(!cs_step(&b,0,0,1));assert(cs_poll(&b,1,"1234ABCD",8,out)==33);
 assert(cs_reply(out,33,0x1234abcd,&s,&m)&&m==0);n++;
 for(unsigned i=0;i<33;i++){char bad[36];memcpy(bad,out,36);bad[i]^=1;assert(!cs_reply(bad,33,0x1234abcd,&s,&m));n++;}
 for(unsigned i=0;i<33;i++){assert(!cs_reply(out,i,0x1234abcd,&s,&m));n++;}
 assert(!cs_reply(out,33,9,&s,&m));n++;
 cs_step(&b,10,2,1);assert(cs_step(&b,200,0,1));n++;
 cs_step(&b,300,2,1);cs_poll(&b,600,"1234ABCD",8,out);cs_step(&b,800,2,1);assert(b.mode==1&&b.seq==1);n++;
 cs_step(&b,810,0,1);assert(b.mode==2&&b.seq==2);n++;
 cs_step(&b,900,2,1);cs_poll(&b,1300,"1234ABCD",8,out);cs_step(&b,1400,2,1);assert(b.mode==1);cs_step(&b,1650,2,1);assert(b.mode==3);n++;
 cs_step(&b,1700,0,1);cs_step(&b,1800,2,1);cs_poll(&b,2200,"1234ABCD",8,out);cs_step(&b,2300,2,1);assert(b.mode==1);cs_step(&b,2310,3,1);assert(b.mode==3);n++;
 assert(!cs_poll(&b,2400,"FFFFFFFFFFFFFFFF",16,out));assert(!cs_poll(&b,2400,"AT+RST??",8,out));n+=2;
 printf("PASS %d protocol/lease/gesture checks; host only\n",n);
}
