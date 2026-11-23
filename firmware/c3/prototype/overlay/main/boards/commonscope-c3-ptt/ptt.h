#ifndef CS_PTT_H
#define CS_PTT_H
#include <stdint.h>
/* Caller supplies the already debounced native key: 0=none, 1=minus, 2=plus.
   Inputs sampled continuously; allowed is false on modals, sleep, etc.
   START requires a live, handshaken C3. Dispatch CANCEL without submitting. */
enum ptt_event { QUIET, SHOW_IP, START_AUDIO, SUBMIT_AUDIO, CANCEL_AUDIO };
enum ptt_state { REQUIRE_RELEASE, IDLE, PLUS_HELD, RECORDING, MINUS_HELD };
typedef struct { enum ptt_state state; uint32_t down; } Ptt;
static inline enum ptt_event ptt_step(Ptt *p,uint32_t ms,unsigned key,int allowed,int linked){
    if(!allowed || key>2 || (p->state==RECORDING && !linked)){
        int active=p->state==RECORDING;
        p->state=REQUIRE_RELEASE;
        return active?CANCEL_AUDIO:QUIET;
    }
    if(p->state==REQUIRE_RELEASE){ if(!key)p->state=IDLE; return QUIET; }
    if(p->state==IDLE){
        if(key){ p->down=ms; p->state=key==2?PLUS_HELD:MINUS_HELD; }
        return QUIET;
    }
    if(p->state==RECORDING){
        if(key==2)return QUIET;
        p->state=key?REQUIRE_RELEASE:IDLE;
        return key?CANCEL_AUDIO:SUBMIT_AUDIO;
    }
    int plus=p->state==PLUS_HELD;
    if(!key){
        uint32_t held=ms-p->down;
        p->state=IDLE;
        return !plus || held<500u ? SHOW_IP:QUIET;
    }
    if(key!=(plus?2u:1u)){ p->state=REQUIRE_RELEASE;return QUIET; }
    if(plus && ms-p->down>=500u){
        p->state=linked?RECORDING:REQUIRE_RELEASE;
        return linked?START_AUDIO:QUIET;
    }
    return QUIET;
}
#endif
