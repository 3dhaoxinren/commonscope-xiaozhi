#include "commonscope_ptt.h"
int commonscope_ptt_step(CommonScopePtt*c,uint32_t now,unsigned raw_keys,int blocked){
 if(!c)return 0;
 uint32_t token=cs_port_lock();
 if(c->raw!=raw_keys){c->raw=raw_keys;c->raw_since=now;}
 if((uint32_t)(now-c->raw_since)>=50)c->stable=c->raw;
 int show=cs_step(&c->bridge,now,c->stable,!blocked);
 cs_port_unlock(token);return show;
}
int commonscope_ptt_query(CommonScopePtt*c,uint32_t now,const char*arg,unsigned n){
 uint32_t nonce;if(!c||!arg||n!=8||!cs_unhex(arg,8,&nonce))return -1;
 char response[36];uint32_t token=cs_port_lock();
 int length=cs_poll(&c->bridge,now,arg,n,response);
 cs_port_unlock(token);
 if(length)cs_port_uart_response(response,(unsigned)length);
 return length?2:-1; /* ATCMD_RESULT_DONE or ERR; preserve native protocol. */
}
