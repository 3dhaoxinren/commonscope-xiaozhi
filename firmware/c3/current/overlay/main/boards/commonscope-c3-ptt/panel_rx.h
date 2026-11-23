#pragma once
#include "panel_wire.h"
#include "ptt.h"
// Only valid nonce-matched responses get here. Require neutral on (re)connect.
struct PanelGesture {
    Ptt p{};
    uint32_t seen = 0, started = 0;
    uint16_t seq = 0;
    uint8_t key = 3;
    bool live = false;
    unsigned Break() {
        unsigned e = p.state == RECORDING ? 3 : 0;
        p.state = REQUIRE_RELEASE;
        live = false;
        return e;
    }
    unsigned Expire(uint32_t now) { return live && now - seen >= 350 ? Break() : 0; }
    unsigned Sample(uint32_t now, uint16_t s, unsigned k, bool idle, bool active) {
        if (k > 3)
            return 0;
        if (!live) {
            live = true;
            seq = s;
            key = k;
            seen = now;
            p.state = k == 0 ? IDLE : REQUIRE_RELEASE;
            return 0;
        }
        uint16_t d = s - seq;
        if (d >= 0x8000u || (d == 0 && key != k))
            return 0;
        unsigned forced = 0;
        if (d > 1) {
            forced = p.state == RECORDING ? 3 : 0;
            p.state = REQUIRE_RELEASE;
        }
        seen = now;
        seq = s;
        key = k;
        if (p.state == RECORDING && (now - started >= 30000 || !active)) {
            p.state = REQUIRE_RELEASE;
            return 3;
        }
        auto e = ptt_step(&p, now, k, idle || (p.state == RECORDING && active), true);
        if (e == START_AUDIO) {
            started = now;
            return 1;
        }
        return forced ? forced : e == SUBMIT_AUDIO ? 2 : e == CANCEL_AUDIO ? 3 : 0;
    }
};
struct PanelLine {
    char buf[21]{};
    unsigned used = 0;
    bool Feed(uint8_t ch, uint32_t& n, uint16_t& seq, unsigned& key) {
        if (ch == '@') {
            used = 1;
            buf[0] = '@';
            return false;
        }
        if (ch == '\n' || ch == '\r') {
            unsigned len = used;
            used = 0;
            uint32_t s, k;
            if (pcheck(buf, len, "@P2|", 17) && phread(buf + 4, 8, &n) && phread(buf + 12, 4, &s) &&
                phread(buf + 16, 1, &k) && k <= 3) {
                seq = s;
                key = k;
                return true;
            }
            return false;
        }
        if (used && used < sizeof(buf) && ch >= 32 && ch < 127)
            buf[used++] = (char)ch;
        else
            used = 0;
        return false;
    }
};
