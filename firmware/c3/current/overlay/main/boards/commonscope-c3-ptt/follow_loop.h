#pragma once
#include <cstdint>
// Only the application main task owns this state. No network or clock calls in the model.
struct CsFollow {
 enum Phase {Off, WaitReply, Talking, Drain, Starting, Mic};
 enum Action {None, Listen, End};
 unsigned end_code=0;
 Phase p=Off; uint32_t since=0,voice_at=0,count=0,quiet_at=0;bool drained=false,resumed=false;
 void enter(Phase x,uint32_t now){p=x;since=now;drained=false;}
 void clear(){p=Off;resumed=false;drained=false;}
 void first_wake(uint32_t now){resumed=false;enter(Starting,now);}
 void submit(uint32_t now){resumed=false;enter(WaitReply,now);}
 void recognized(uint32_t now){if(p==Mic) enter(WaitReply,now);}
 void talking(uint32_t now){if(p!=Off)enter(Talking,now);}
 void stop_tts(uint32_t now){if(p==Talking)enter(Drain,now);}
 void opened(uint32_t now,uint32_t n){if(p==Starting){enter(Mic,now);voice_at=now;count=n;}}
 Action end(unsigned why){end_code=why;return End;}
 Action poll(uint32_t now,bool allowed,bool empty,uint32_t last_voice_ms){
   if(p==Off)return None;
   if(!allowed)return end(13);
   switch(p){
    case Mic:
     // Record only a newer audio-frame timestamp in THIS mic window, never the delayed poll time.
     if(last_voice_ms && (int32_t)(last_voice_ms-voice_at)>0 && (int32_t)(now-last_voice_ms)>=0)voice_at=last_voice_ms;
     if(now-voice_at>=10000u)return end(10);
     return now-since>=45000u ? end(11):None;
    case WaitReply:return now-since>=10000u ? end(12):None;
    case Talking:return now-since>=180000u ? end(14):None;
    case Drain:
     if(now-since>=35000u)return end(14);
     if(!empty){drained=false;return None;}
     if(!drained){drained=true;quiet_at=now;return None;}
     if(now-quiet_at>=250u){enter(Starting,now);resumed=true;return Listen;}
     return None;
    case Starting:return now-since>=5000u ? end(14):None;
    default:return None;
   }
 }
};
