#pragma once
#include <cstdint>
namespace cs_lan {
enum Event : uint16_t {
    Boot = 1,
    Key,
    PanelLink,
    State,
    MicPeak,
    MicTimeout,
    Recognized,
    Reply,
    Play,
    Window,
    Error,
    Verified,
    BootAccepted,
    Rollback,
    AudioSent,
    AudioSendFail
};
void note(Event e, uint32_t value = 0);
void mic(const int16_t* pcm, int n);
void sent(bool ok);
bool busy();
bool try_weather();
void end_weather();
void tick(uint32_t now, bool linked, bool idle, bool fresh, unsigned key, unsigned state);
}  // namespace cs_lan
