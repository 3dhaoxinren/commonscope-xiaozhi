#ifndef CS_PTT_RECEIVER_H
#define CS_PTT_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

enum {
    CS_RX_NONE = 0,
    CS_RX_START = 1,
    CS_RX_SUBMIT = 2,
    CS_RX_CANCEL = 3,
    CS_RX_LEASE_MS = 350,
};

typedef struct {
    uint32_t seq, mode, seen;
    bool valid, owned;
} CsPttReceiver;

static inline unsigned cs_rx_expire(CsPttReceiver *r, uint32_t now) {
    if (!r->valid || (uint32_t)(now - r->seen) < CS_RX_LEASE_MS) return CS_RX_NONE;
    bool cancel = r->owned;
    r->owned = r->valid = false;
    return cancel ? CS_RX_CANCEL : CS_RX_NONE;
}

/* Caller expires the lease first and passes only nonce/CRC-verified frames. */
static inline unsigned cs_rx_accept(CsPttReceiver *r, uint32_t now,
                                    uint32_t seq, uint32_t mode, bool can_start) {
    if (mode > CS_RX_CANCEL) return CS_RX_NONE;
    if (!r->valid) {
        /* Never start from stale recording state after boot/reconnect. */
        r->valid = true;
        r->seen = now;
        r->seq = seq;
        r->mode = mode;
        return CS_RX_NONE;
    }
    uint32_t forward = seq - r->seq;
    if (forward == 0) {
        if (mode == r->mode) r->seen = now;
        return CS_RX_NONE;
    }
    if (forward >= UINT32_C(0x80000000)) return CS_RX_NONE;
    r->seen = now;
    r->seq = seq;
    r->mode = mode;
    if (r->owned) {
        r->owned = false;
        /* Missing a transition could associate STOP with the wrong recording. */
        return (forward == 1 && mode == CS_RX_SUBMIT) ? CS_RX_SUBMIT : CS_RX_CANCEL;
    }
    if (mode == CS_RX_START && can_start) {
        r->owned = true;
        return CS_RX_START;
    }
    return CS_RX_NONE;
}
#endif
