#pragma once
#include <atomic>
#include <algorithm>
#include <cstring>
#include "esp_vad.h"
#include "esp_timer.h"
// Single audio producer, fixed mono 16 kHz board. Run VAD only during auto listening.
namespace cs_fvad {
static std::atomic<uint32_t> gate{0}, pulses{0}, last_voice{0};
inline bool audible(const int16_t* frame){
 int sum=0,clip=0,dev=0;
 for(int i=0;i<320;i++){int v=frame[i];sum+=v;clip+=(v>32200||v< -32200);}
 if(clip>40)return false;
 int mean=sum/320;
 for(int i=0;i<320;i++){int v=int(frame[i])-mean;dev+=v<0?-v:v;}
 return dev>=320*110; // DC and a very low noise floor cannot keep the conversation alive
}
inline void pcm(const int16_t* p,int n){
 static vad_handle_t handle=nullptr; static uint32_t owner=0; static int used=0;
 static int16_t buf[320];
 const uint32_t tag=gate.load();
 if(!tag){if(handle){vad_destroy(handle);handle=nullptr;}owner=0;used=0;return;}
 if(tag!=owner){if(handle)vad_destroy(handle);handle=vad_create_with_param(VAD_MODE_2,16000,20,100,140);owner=tag;used=0;}
 if(!handle || !p)return; // timer remains fail-closed, not indefinite
 while(n>0){int k=std::min(n,320-used);memcpy(buf+used,p,k*sizeof(*p));p+=k;n-=k;used+=k;
   if(used==320){used=0;if(vad_process_with_trigger(handle,buf)==VAD_SPEECH && audible(buf) && gate.load()==tag){last_voice.store((uint32_t)(esp_timer_get_time()/1000));pulses.fetch_add(1);}}
 }
}
}
extern "C" unsigned cs_follow_audio_gate(bool on){
 static uint32_t generation=0; // application main task is sole writer
 if(on){cs_fvad::last_voice.store(0);if(!++generation)++generation;cs_fvad::gate.store(generation);}else cs_fvad::gate.store(0);
 return cs_fvad::pulses.load();
}
extern "C" unsigned cs_follow_pulses(){return cs_fvad::last_voice.load();}
