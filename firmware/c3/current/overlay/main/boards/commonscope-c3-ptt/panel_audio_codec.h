#pragma once

#include <mutex>
#include "codecs/no_audio_codec.h"
#include "cs_lan.h"

// This board initializes TX first: it owns the shared BCLK/WS; RX is its slave.
// Serialize both power transitions, including requests already posted by the idle timer.
class PanelAudioCodec final : public NoAudioCodecDuplex {
public:
    using NoAudioCodecDuplex::NoAudioCodecDuplex;

    void EnableInput(bool enable) override {
        std::lock_guard<std::mutex> pair(power_mutex_);
        if (enable && !output_enabled()) {
            NoAudioCodecDuplex::EnableOutput(true);
        }
        NoAudioCodecDuplex::EnableInput(enable);
    }

    void EnableOutput(bool enable) override {
        std::lock_guard<std::mutex> pair(power_mutex_);
        if (!enable && input_enabled()) {
            return;
        }
        NoAudioCodecDuplex::EnableOutput(enable);
    }

protected:
    int Read(int16_t* dst, int n) override {
        int got = NoAudioCodecDuplex::Read(dst, n);
        cs_lan::mic(dst, got);
        return got;
    }

private:
    std::mutex power_mutex_;
};
