#ifndef CS_BRIDGE_H
#define CS_BRIDGE_H
#include <stdint.h>
#include "ptt.h"
/* Fixed 33-character response. ASCII survives native UART logs.
   +CSNG:NNNNNNNN,EEEEEEEE,SS,CCCC\r\n
   N=request nonce, E=transition sequence, S=0idle/1record/2submit/3cancel.
   CRC16/CCITT over first27 bytes, initial0xffff. No pointers/Flash/AT passthrough. */
typedef struct { Ptt ptt; uint32_t seq, mode, since, linked, start; } CsBridge;
static inline uint16_t cs_crc(const char *s,unsigned n){uint16_t c=0xffff;for(unsigned i=0;i<n;i++){c^=(uint16_t)(uint8_t)s[i]<<8;for(unsigned j=0;j<8;j++)c=(uint16_t)((c&0x8000)?(c<<1)^0x1021:c<<1);}return c;}
static inline void cs_hex(char *s,uint32_t x,unsigned n){static const char h[]="0123456789ABCDEF";while(n){s[--n]=h[x&15];x>>=4;}}
static inline int cs_unhex(const char*s,unsigned n,uint32_t*out){uint32_t x=0;for(unsigned i=0;i<n;i++){unsigned d=(unsigned char)s[i];if(d>='0'&&d<='9')d-='0';else if(d>='A'&&d<='F')d=d-'A'+10;else return 0;x=(x<<4)|d;}*out=x;return 1;}
static inline int cs_step(CsBridge*b,uint32_t now,unsigned key,int allowed){
 int live=b->linked&&(uint32_t)(now-b->since)<350;
 enum ptt_event e=ptt_step(&b->ptt,now,key,allowed,live);
 if(b->mode==1&&(uint32_t)(now-b->start)>=30000){b->ptt.state=REQUIRE_RELEASE;e=CANCEL_AUDIO;}
 if(e==START_AUDIO){b->mode=1;b->start=now;b->seq++;}
 if(e==SUBMIT_AUDIO){b->mode=2;b->seq++;}
 if(e==CANCEL_AUDIO){b->mode=3;b->seq++;}
 return e==SHOW_IP;
}
/* Caller serializes poll/step. AT handler should enqueue poll; do not race worker. */
static inline int cs_poll(CsBridge*b,uint32_t now,const char*nonce,unsigned n,char out[36]){
 uint32_t x;if(n!=8||!cs_unhex(nonce,8,&x))return 0;
 b->linked=1;b->since=now;
 const char prefix[]="+CSNG:";for(unsigned i=0;i<6;i++)out[i]=prefix[i];
 cs_hex(out+6,x,8);out[14]=',';cs_hex(out+15,b->seq,8);out[23]=',';
 cs_hex(out+24,b->mode,2);out[26]=',';cs_hex(out+27,cs_crc(out,27),4);
 out[31]='\r';out[32]='\n';out[33]=0;return 33;
}
static inline int cs_reply(const char*s,unsigned n,uint32_t nonce,uint32_t*seq,uint32_t*mode){
 if(n!=33)return 0;
 const char p[]="+CSNG:";
 for(unsigned i=0;i<6;i++)if(s[i]!=p[i])return 0;
 if(s[14]!=','||s[23]!=','||s[26]!=','||s[31]!='\r'||s[32]!='\n')return 0;
 uint32_t nn,ss,mm,crc;if(!cs_unhex(s+6,8,&nn)||nn!=nonce||!cs_unhex(s+15,8,&ss)||!cs_unhex(s+24,2,&mm)||mm>3||!cs_unhex(s+27,4,&crc)||crc!=cs_crc(s,27))return 0;
 *seq=ss;*mode=mm;return 1;
}
#endif
